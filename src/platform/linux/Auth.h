#pragma once

#include <array>
#include <string>
#include <openssl/crypto.h>
#include <openssl/sha.h>

#include "core/api/ApiRouter.h"

namespace awtrix::auth {
using Digest = std::array<unsigned char, SHA256_DIGEST_LENGTH>;

inline Digest digest(const std::string& value) {
  Digest result{};
  SHA256(reinterpret_cast<const unsigned char*>(value.data()), value.size(), result.data());
  return result;
}
inline void wipe(std::string& value) {
  if (!value.empty()) OPENSSL_cleanse(value.data(), value.size());
  value.clear();
}
inline bool equal(const Digest& a, const Digest& b) {
  return CRYPTO_memcmp(a.data(), b.data(), a.size()) == 0;
}

// The listener supplies its own realm/message; both refuse the connection in the same way.
template <typename Response>
bool deny(Response& response, const api::HttpResult& result) {
  response.status = result.status;
  response.set_header("Connection", "close");
  response.set_content(result.body, result.contentType);
  return false;
}
template <typename Response>
bool deny(Response& response, int status, const char* code, const char* message) {
  return deny(response, api::errorResult(status, code, message));
}
template <typename Response>
bool unauthorized(Response& response, const char* challenge, const char* message = nullptr) {
  response.set_header("WWW-Authenticate", challenge);
  return deny(response, message ? api::unauthorized(message) : api::unauthorized());
}
}
