#pragma once

#include <cstdint>
#include <cstdio>
#include <string>
#include <string_view>

namespace awtrix {
namespace tc002d {
namespace ip {

inline bool parseIpv4(std::string_view text, uint32_t& out) {
  uint32_t address = 0;
  std::size_t at = 0;
  for (int part = 0; part < 4; ++part) {
    const std::size_t begin = at;
    unsigned value = 0;
    while (at < text.size() && text[at] >= '0' && text[at] <= '9' && at - begin < 3)
      value = value * 10 + static_cast<unsigned>(text[at++] - '0');
    if (at == begin || value > 255 || (at - begin > 1 && text[begin] == '0')) return false;
    address = (address << 8) | value;
    if (part < 3 && (at >= text.size() || text[at++] != '.')) return false;
  }
  if (at != text.size()) return false;
  out = address;
  return true;
}

inline std::string formatIpv4(uint32_t address) {
  char text[16];
  std::snprintf(text, sizeof(text), "%u.%u.%u.%u", address >> 24, (address >> 16) & 255,
                (address >> 8) & 255, address & 255);
  return text;
}

inline uint32_t prefixMask(unsigned prefix) {
  return prefix == 0 ? 0 : prefix >= 32 ? UINT32_MAX : UINT32_MAX << (32 - prefix);
}

inline bool maskPrefix(uint32_t mask, uint8_t& prefix) {
  uint8_t bits = 0;
  while (bits < 32 && (mask & (UINT32_C(0x80000000) >> bits))) ++bits;
  if (prefixMask(bits) != mask) return false;
  prefix = bits;
  return true;
}

inline bool isUnicast(uint32_t address) {
  const uint32_t first = address >> 24;
  return first != 0 && first != 127 && first < 224;
}

inline bool sameSubnet(uint32_t a, uint32_t b, unsigned prefix) {
  return ((a ^ b) & prefixMask(prefix)) == 0;
}

}
}
}
