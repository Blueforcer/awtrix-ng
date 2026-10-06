#include "../support.h"
// Authorize URLs, token requests and token responses of the script sign-in.
#include <cstdio>
#include <string>

#include "platform/linux/net/UrlEncode.h"
#include "platform/linux/oauth/OAuthFlow.h"
#include "platform/linux/oauth/OAuthSpec.h"
#include "OAuthTestSupport.h"

using namespace awtrix;

namespace {

int& failures = awtrix::test::failures();

using awtrix::test::check;

}

int main() {
  check(net::percentEncoded("a b/c?d=\xC3\xA9~") == "a%20b%2Fc%3Fd%3D%C3%A9~", "RFC 3986 unreserved set only");
  check(net::formEncoded({{"grant_type", "authorization_code"}, {"code", "a+b"}}) ==
            "grant_type=authorization_code&code=a%2Bb",
        "form body joins encoded pairs");
  check(net::formEncoded({}).empty(), "empty form");

  // base64url(SHA-256(verifier)) without padding, as Python's hashlib and base64 compute it.
  check(oauth::pkceChallenge("dBjftJeZ4CVP-mB92K27uhbUJU1p1r_wW1gFWjVXtjk") ==
            "AJTOGEIkYnnG3CvOIzSjNGaGmgvmXOMwbhQgRKJLr4Q",
        "S256 challenge");
  const std::string r1 = oauth::randomToken(32), r2 = oauth::randomToken(32);
  check(r1.size() == 43 && r1 != r2 && r1.find_first_of("+/=") == std::string::npos, "random base64url tokens");

  for (const char* app : {"Spotify", "A", "Ab", "Abc", "\xC3\x9C" "ber"}) {
    const std::string state = oauth::makeState("http://192.168.1.50", app, "n0nce");
    check(state.find_first_of("+/=") == std::string::npos, "state is base64url");
    check(oauth_test::stateMember(state, "r") == "http://192.168.1.50" && oauth_test::stateMember(state, "a") == app,
          "state carries the way back and the app");
  }

  std::string error;
  const auto spec = oauth::parseSpec(
      "# @oauth authorize=https://accounts.spotify.com/authorize token=https://accounts.spotify.com/api/token "
      "api=api.spotify.com scope=\"a b\" pkce params=\"show_dialog=true\"\n",
      &error);
  check(spec.has_value(), "spec parses");
  if (!spec) return 1;
  const std::string url = oauth::authorizeUrl(*spec, "cid", "ST", "CH");
  check(url == "https://accounts.spotify.com/authorize?response_type=code&client_id=cid"
               "&redirect_uri=https%3A%2F%2Fawtrix.de%2Foauth%2Fcallback&scope=a%20b&state=ST"
               "&code_challenge=CH&code_challenge_method=S256&show_dialog=true",
        "authorize url");

  const auto basic = oauth::exchangeRequest(*spec, {"cid", "sec"}, "c0de", "ver");
  check(basic.method == "POST" && basic.url == "https://accounts.spotify.com/api/token",
        "exchange goes to the token endpoint");
  check(basic.body == "grant_type=authorization_code&code=c0de"
                      "&redirect_uri=https%3A%2F%2Fawtrix.de%2Foauth%2Fcallback&code_verifier=ver",
        "exchange body with verifier");
  check(oauth_test::headerOf(basic, "authorization") == "Basic Y2lkOnNlYw==", "basic client auth");
  check(oauth_test::headerOf(basic, "Content-Type") == "application/x-www-form-urlencoded", "form content type");

  const auto publicClient = oauth::refreshRequest(*spec, {"cid", ""}, "rt");
  check(publicClient.body == "grant_type=refresh_token&refresh_token=rt&client_id=cid", "public client refresh");
  check(oauth_test::headerOf(publicClient, "Authorization").empty(), "no basic header without a secret");

  auto bodySpec = *spec;
  bodySpec.clientAuth = oauth::ClientAuth::Body;
  check(oauth::refreshRequest(bodySpec, {"cid", "sec"}, "rt").body ==
            "grant_type=refresh_token&refresh_token=rt&client_id=cid&client_secret=sec",
        "body client auth");

  const auto ok = oauth::parseTokenResponse(
      200, R"({"access_token":"AT","token_type":"Bearer","expires_in":3600,"refresh_token":"RT","scope":"a b"})");
  check(ok.ok && ok.accessToken == "AT" && ok.refreshToken == "RT" && ok.expiresIn == 3600, "token response");
  const auto bad = oauth::parseTokenResponse(400, R"({"error":"invalid_grant","error_description":"x"})");
  check(!bad.ok && bad.error == "invalid_grant" && bad.permanent, "invalid_grant is permanent");
  const auto down = oauth::parseTokenResponse(503, "<html>");
  check(!down.ok && !down.permanent && down.error == "HTTP 503", "server errors are temporary");
  const auto offline = oauth::parseTokenResponse(0, "");
  check(!offline.ok && !offline.permanent && offline.error == "no connection", "no connection");
  const auto noBearer = oauth::parseTokenResponse(200, R"({"access_token":"AT","token_type":"mac"})");
  check(!noBearer.ok, "only bearer tokens");
  const auto okError = oauth::parseTokenResponse(200, R"({"error":"bad_verification_code"})");
  check(!okError.ok && okError.error == "bad_verification_code", "an error inside a 200 answer");
  if (failures == 0) std::puts("ok");
  return failures == 0 ? 0 : 1;
}
