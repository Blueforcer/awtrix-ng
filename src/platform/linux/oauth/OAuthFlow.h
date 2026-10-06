#pragma once

#include <cstddef>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "platform/linux/oauth/OAuthSpec.h"

namespace awtrix::oauth {

// Registered once at the provider; the Hub hands the code on to the clock's own page.
inline constexpr const char* kRedirectUri = "https://awtrix.de/oauth/callback";

using Headers = std::vector<std::pair<std::string, std::string>>;

struct Credentials {
  std::string clientId;
  std::string clientSecret;
};

struct HttpCall {
  std::string method = "GET";
  std::string url;
  std::string body;
  Headers headers;
  std::size_t cap = 65536;
};

std::string base64(const unsigned char* data, std::size_t size);
std::string base64Url(const unsigned char* data, std::size_t size);
// base64url of `bytes` random bytes; "" when the generator fails.
std::string randomToken(std::size_t bytes);
std::string sha256Hex(std::string_view text);
// RFC 7636 S256.
std::string pkceChallenge(const std::string& verifier);

std::string makeState(const std::string& returnOrigin, const std::string& app, const std::string& nonce);

std::string authorizeUrl(const Spec& spec, const std::string& clientId, const std::string& state,
                         const std::string& challenge);
HttpCall exchangeRequest(const Spec& spec, const Credentials& c, const std::string& code, const std::string& verifier);
HttpCall refreshRequest(const Spec& spec, const Credentials& c, const std::string& refreshToken);

struct TokenResult {
  bool ok = false;
  // The refresh token is gone for good; only a new sign-in helps.
  bool permanent = false;
  std::string accessToken;
  std::string refreshToken;
  long long expiresIn = 0;
  std::string error;
};
TokenResult parseTokenResponse(int status, std::string_view body);

}
