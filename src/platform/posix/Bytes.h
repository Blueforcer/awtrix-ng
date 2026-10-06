#pragma once

#include <cstddef>
#include <cstdint>
#include <string_view>
#include <vector>

namespace awtrix::posix {

inline constexpr int hexValue(char c) {
  if (c >= '0' && c <= '9') return c - '0';
  if (c >= 'a' && c <= 'f') return c - 'a' + 10;
  if (c >= 'A' && c <= 'F') return c - 'A' + 10;
  return -1;
}

inline bool fromHex(std::string_view text, std::vector<uint8_t>& out) {
  if (text.size() % 2) return false;
  for (char c : text) if (hexValue(c) < 0) return false;
  out.resize(text.size() / 2);
  for (std::size_t i = 0; i < out.size(); ++i)
    out[i] = static_cast<uint8_t>((hexValue(text[i * 2]) << 4) | hexValue(text[i * 2 + 1]));
  return true;
}

// Exactly six colon-separated pairs; trimming belongs to the text transport.
inline bool parseMac(std::string_view text, uint8_t* out, bool reversed = false) {
  if (text.size() != 17) return false;
  uint8_t bytes[6];
  for (std::size_t i = 0; i < 6; ++i) {
    const int high = hexValue(text[i * 3]), low = hexValue(text[i * 3 + 1]);
    if (high < 0 || low < 0 || (i < 5 && text[i * 3 + 2] != ':')) return false;
    bytes[reversed ? 5 - i : i] = static_cast<uint8_t>((high << 4) | low);
  }
  for (std::size_t i = 0; i < 6; ++i) out[i] = bytes[i];
  return true;
}

inline constexpr uint16_t le16(const uint8_t* p) {
  return static_cast<uint16_t>(p[0] | uint16_t(p[1]) << 8);
}
inline constexpr uint32_t le32(const uint8_t* p) {
  return uint32_t(p[0]) | uint32_t(p[1]) << 8 | uint32_t(p[2]) << 16 | uint32_t(p[3]) << 24;
}
inline void put16(uint8_t* p, uint16_t value) {
  p[0] = static_cast<uint8_t>(value);
  p[1] = static_cast<uint8_t>(value >> 8);
}
inline void put32(uint8_t* p, uint32_t value) {
  for (unsigned i = 0; i < 4; ++i) p[i] = static_cast<uint8_t>(value >> (i * 8));
}
inline void put16(std::vector<uint8_t>& out, uint16_t value) {
  const auto at = out.size();
  out.resize(at + 2);
  put16(out.data() + at, value);
}
inline void put32(std::vector<uint8_t>& out, uint32_t value) {
  const auto at = out.size();
  out.resize(at + 4);
  put32(out.data() + at, value);
}
}
