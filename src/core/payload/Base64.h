#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace awtrix::base64 {

inline constexpr char kAlphabet[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";

// Standard padded Base64, or unpadded URL-safe Base64 (RFC 4648 section 5).
inline std::string encode(const void* data, std::size_t size, bool url = false) {
  if (!data || !size) return {};
  const auto* bytes = static_cast<const uint8_t*>(data);
  std::string out;
  if (size / 3 > (out.max_size() - 4) / 4) return {};
  out.reserve(size / 3 * 4 + (size % 3 ? 4 : 0));
  uint32_t bits = 0;
  unsigned count = 0;
  const auto append = [&](unsigned value) {
    out += url && value >= 62 ? (value == 62 ? '-' : '_') : kAlphabet[value];
  };
  for (std::size_t i = 0; i < size; ++i) {
    bits = (bits << 8) | bytes[i];
    count += 8;
    while (count >= 6) { count -= 6; append((bits >> count) & 63u); }
  }
  if (count) append((bits << (6 - count)) & 63u);
  if (!url) while (out.size() % 4) out += '=';
  return out;
}

// Standard alphabet, padding optional. Leftover bits that do not complete a byte are dropped.
inline int sextet(char c) {
  if (c >= 'A' && c <= 'Z') return c - 'A';
  if (c >= 'a' && c <= 'z') return c - 'a' + 26;
  if (c >= '0' && c <= '9') return c - '0' + 52;
  if (c == '+') return 62;
  if (c == '/') return 63;
  return -1;
}

inline std::size_t unpadded(const char* s, std::size_t len) {
  while (len > 0 && s[len - 1] == '=') --len;
  return len;
}

inline std::size_t decodedSize(const char* s, std::size_t len) {
  return unpadded(s, len) * 3 / 4;
}

inline bool valid(const char* s, std::size_t len) {
  if (s == nullptr) return false;
  const std::size_t n = unpadded(s, len);
  for (std::size_t i = 0; i < n; ++i)
    if (sextet(s[i]) < 0) return false;
  return true;
}

// out must hold decodedSize(s, len) bytes.
inline bool decode(const char* s, std::size_t len, uint8_t* out, std::size_t& written) {
  written = 0;
  if (s == nullptr) return false;
  const std::size_t n = unpadded(s, len);
  uint32_t acc = 0;
  int bits = 0;
  for (std::size_t i = 0; i < n; ++i) {
    const int v = sextet(s[i]);
    if (v < 0) {
      written = 0;
      return false;
    }
    acc = (acc << 6) | static_cast<uint32_t>(v);
    bits += 6;
    if (bits >= 8) {
      bits -= 8;
      out[written++] = static_cast<uint8_t>((acc >> bits) & 0xFFu);
    }
  }
  return true;
}

inline bool decode(const char* s, std::size_t len, std::vector<uint8_t>& out) {
  out.clear();
  if (s == nullptr) return false;
  out.resize(decodedSize(s, len));
  std::size_t written = 0;
  if (!decode(s, len, out.data(), written)) {
    out.clear();
    return false;
  }
  out.resize(written);
  return true;
}

}
