// What the script sign-in tests read back from requests and states.
#pragma once

#include <cctype>
#include <string>
#include <string_view>

#include "core/api/JsonReader.h"
#include "platform/linux/oauth/OAuthFlow.h"

namespace oauth_test {

inline std::string headerOf(const awtrix::oauth::HttpCall& call, std::string_view name) {
  for (const auto& [key, value] : call.headers) {
    if (key.size() != name.size()) continue;
    bool same = true;
    for (std::size_t i = 0; i < key.size() && same; ++i)
      same = std::tolower(static_cast<unsigned char>(key[i])) == std::tolower(static_cast<unsigned char>(name[i]));
    if (same) return value;
  }
  return {};
}

// A member of the base64url JSON in a state; "" when the state is not one.
inline std::string stateMember(const std::string& state, const char* name) {
  static const std::string kAlphabet = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789-_";
  std::string json;
  unsigned bits = 0;
  int count = 0;
  for (const char c : state) {
    const std::size_t v = kAlphabet.find(c);
    if (v == std::string::npos) return {};
    bits = (bits << 6) | static_cast<unsigned>(v);
    count += 6;
    if (count >= 8) {
      count -= 8;
      json += static_cast<char>((bits >> count) & 0xFF);
    }
  }
  if (!awtrix::api::isWellFormed(json)) return {};
  const auto member = awtrix::api::memberValue(awtrix::api::JsonReader(json), name);
  std::string out;
  if (awtrix::api::present(member)) member.appendString(out);
  return out;
}

}
