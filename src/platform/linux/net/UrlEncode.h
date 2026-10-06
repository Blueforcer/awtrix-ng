#pragma once

#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace awtrix::net {

// RFC 3986: everything but the unreserved characters as %XX in upper case.
inline std::string percentEncoded(std::string_view text) {
  static const char kHex[] = "0123456789ABCDEF";
  std::string out;
  out.reserve(text.size());
  for (const char c : text) {
    const auto b = static_cast<unsigned char>(c);
    if ((b >= 'A' && b <= 'Z') || (b >= 'a' && b <= 'z') || (b >= '0' && b <= '9') || b == '-' || b == '.' ||
        b == '_' || b == '~') {
      out += c;
    } else {
      out += '%';
      out += kHex[b >> 4];
      out += kHex[b & 15];
    }
  }
  return out;
}

// An application/x-www-form-urlencoded body or query string.
inline std::string formEncoded(const std::vector<std::pair<std::string, std::string>>& fields) {
  std::string out;
  for (const auto& [key, value] : fields) {
    if (!out.empty()) out += '&';
    out += percentEncoded(key);
    out += '=';
    out += percentEncoded(value);
  }
  return out;
}

}
