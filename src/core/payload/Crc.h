#pragma once

#include <array>
#include <cstddef>
#include <cstdint>

namespace awtrix {

namespace crc_detail {
constexpr std::array<uint32_t, 256> table() {
  std::array<uint32_t, 256> out{};
  for (uint32_t i = 0; i < out.size(); ++i) {
    uint32_t c = i;
    for (int bit = 0; bit < 8; ++bit) c = (c & 1u) ? (0xedb88320u ^ (c >> 1)) : (c >> 1);
    out[i] = c;
  }
  return out;
}
// One read-only table across translation units, with no boot-time initialization or RAM copy.
inline constexpr auto kTable = table();
}

// Reflected CRC-32, polynomial 0xedb88320. The caller controls the initial/final XOR;
// ZIP uses ~crc32Update(~0u, ...), whereas the MCU also uses the raw accumulator.
inline constexpr uint32_t crc32Update(uint32_t crc, const uint8_t* data, std::size_t size) {
  for (std::size_t i = 0; i < size; ++i) crc = crc_detail::kTable[(crc ^ data[i]) & 0xffu] ^ (crc >> 8);
  return crc;
}

inline constexpr uint16_t crc16Ccitt(const uint8_t* data, std::size_t size, uint16_t crc = 0xffff) {
  for (std::size_t i = 0; i < size; ++i) {
    crc ^= static_cast<uint16_t>(data[i] << 8);
    for (unsigned bit = 0; bit < 8; ++bit)
      crc = static_cast<uint16_t>((crc << 1) ^ ((crc & 0x8000) ? 0x1021 : 0));
  }
  return crc;
}
}
