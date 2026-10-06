#include "core/payload/Base64.h"
#include "platform/linux/oauth/OAuthFlow.h"

#include <openssl/rand.h>
#include <openssl/sha.h>

#include "core/api/JsonReader.h"
#include "core/api/JsonWriter.h"
#include "platform/linux/net/UrlEncode.h"
#include "platform/linux/oauth/OAuthText.h"
#include "platform/posix/Files.h"

namespace awtrix::oauth {
namespace {

using Form = std::vector<std::pair<std::string, std::string>>;

HttpCall tokenCall(const Spec& spec, Form form, const Credentials& c) {
  HttpCall call;
  call.method = "POST";
  call.url = spec.token;
  if (c.clientSecret.empty()) {
    form.emplace_back("client_id", c.clientId);
  } else if (spec.clientAuth == ClientAuth::Body) {
    form.emplace_back("client_id", c.clientId);
    form.emplace_back("client_secret", c.clientSecret);
  } else {
    const std::string pair = net::percentEncoded(c.clientId) + ":" + net::percentEncoded(c.clientSecret);
    call.headers.emplace_back("Authorization",
                              "Basic " + base64(reinterpret_cast<const unsigned char*>(pair.data()), pair.size()));
  }
  call.body = net::formEncoded(form);
  call.headers.emplace_back("Content-Type", "application/x-www-form-urlencoded");
  call.headers.emplace_back("Accept", "application/json");
  return call;
}

}

std::string base64(const unsigned char* data, std::size_t size) {
  return awtrix::base64::encode(data, size);
}

std::string base64Url(const unsigned char* data, std::size_t size) {
  return awtrix::base64::encode(data, size, true);
}

std::string randomToken(std::size_t bytes) {
  std::vector<unsigned char> raw(bytes);
  if (bytes == 0 || RAND_bytes(raw.data(), static_cast<int>(raw.size())) != 1) return {};
  return base64Url(raw.data(), raw.size());
}

std::string sha256Hex(std::string_view text) {
  unsigned char digest[SHA256_DIGEST_LENGTH];
  SHA256(reinterpret_cast<const unsigned char*>(text.data()), text.size(), digest);
  return posix::hexBytes(digest, sizeof digest);
}

std::string pkceChallenge(const std::string& verifier) {
  unsigned char digest[SHA256_DIGEST_LENGTH];
  SHA256(reinterpret_cast<const unsigned char*>(verifier.data()), verifier.size(), digest);
  return base64Url(digest, sizeof digest);
}

std::string makeState(const std::string& returnOrigin, const std::string& app, const std::string& nonce) {
  std::string json;
  api::JsonWriter(json).beginObject().member("r", returnOrigin).member("a", app).member("n", nonce).endObject();
  return base64Url(reinterpret_cast<const unsigned char*>(json.data()), json.size());
}

std::string authorizeUrl(const Spec& spec, const std::string& clientId, const std::string& state,
                         const std::string& challenge) {
  Form q = {{"response_type", "code"}, {"client_id", clientId}, {"redirect_uri", kRedirectUri}};
  if (!spec.scope.empty()) q.emplace_back("scope", spec.scope);
  q.emplace_back("state", state);
  if (spec.pkce) {
    q.emplace_back("code_challenge", challenge);
    q.emplace_back("code_challenge_method", "S256");
  }
  for (const auto& p : spec.params) q.push_back(p);
  return spec.authorize + (spec.authorize.find('?') == std::string::npos ? "?" : "&") + net::formEncoded(q);
}

HttpCall exchangeRequest(const Spec& spec, const Credentials& c, const std::string& code, const std::string& verifier) {
  Form form = {{"grant_type", "authorization_code"}, {"code", code}, {"redirect_uri", kRedirectUri}};
  if (spec.pkce) form.emplace_back("code_verifier", verifier);
  return tokenCall(spec, std::move(form), c);
}

HttpCall refreshRequest(const Spec& spec, const Credentials& c, const std::string& refreshToken) {
  return tokenCall(spec, {{"grant_type", "refresh_token"}, {"refresh_token", refreshToken}}, c);
}

TokenResult parseTokenResponse(int status, std::string_view body) {
  TokenResult out;
  const bool json = body.size() <= 65536 && api::isWellFormed(body);
  api::JsonReader r(json ? body : std::string_view("{}"));
  if (status >= 200 && status < 300 && json && r.isObject()) {
    out.accessToken = api::memberText(r, "access_token", 8192);
    out.refreshToken = api::memberText(r, "refresh_token", 8192);
    const std::string type = api::memberText(r, "token_type", 32);
    long long expires = 0;
    const auto e = api::memberValue(r, "expires_in");
    if (api::present(e) && e.asLong(expires) && expires > 0) out.expiresIn = expires;
    out.ok = !out.accessToken.empty() && (type.empty() || equalsIgnoringCase(type, "bearer"));
    if (out.ok) return out;
  }
  out.ok = false;
  out.error = json && r.isObject() ? api::memberText(r, "error", 64) : std::string();
  if (out.error.empty())
    out.error = status == 0                      ? "no connection"
                : status >= 200 && status < 300 ? "unusable token response"
                                                 : "HTTP " + std::to_string(status);
  out.permanent = out.error == "invalid_grant" || out.error == "invalid_client" || out.error == "unauthorized_client";
  return out;
}

}
