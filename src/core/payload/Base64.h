#pragma once

#include <cstddef>
#include <cstdint>
#include <vector>

namespace awtrix::base64 {

// Standard alphabet, padding optional. Leftover bits that do not complete a byte are dropped.
inline bool decode(const char* s, std::size_t len, std::vector<uint8_t>& out) {
  out.clear();
  if (s == nullptr) return false;

  auto sextet = [](char c) -> int {
    if (c >= 'A' && c <= 'Z') return c - 'A';
    if (c >= 'a' && c <= 'z') return c - 'a' + 26;
    if (c >= '0' && c <= '9') return c - '0' + 52;
    if (c == '+') return 62;
    if (c == '/') return 63;
    return -1;
  };

  std::size_t n = len;
  while (n > 0 && s[n - 1] == '=') --n;

  out.reserve(n * 3 / 4);
  uint32_t acc = 0;
  int bits = 0;
  for (std::size_t i = 0; i < n; ++i) {
    const int v = sextet(s[i]);
    if (v < 0) {
      out.clear();
      return false;
    }
    acc = (acc << 6) | static_cast<uint32_t>(v);
    bits += 6;
    if (bits >= 8) {
      bits -= 8;
      out.push_back(static_cast<uint8_t>((acc >> bits) & 0xFFu));
    }
  }
  return true;
}

}
