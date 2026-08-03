#pragma once

#include <cstdint>
#include <string>

namespace awtrix {
namespace color {

inline constexpr uint32_t kBlack = 0x000000u;

inline constexpr uint32_t pack(uint8_t r, uint8_t g, uint8_t b) {
  return (static_cast<uint32_t>(r) << 16) | (static_cast<uint32_t>(g) << 8) | b;
}
inline constexpr uint8_t red(uint32_t c) { return static_cast<uint8_t>((c >> 16) & 0xFF); }
inline constexpr uint8_t green(uint32_t c) { return static_cast<uint8_t>((c >> 8) & 0xFF); }
inline constexpr uint8_t blue(uint32_t c) { return static_cast<uint8_t>(c & 0xFF); }

// FastLED-style 8-bit multiply: s is a 0..255 fraction where 255 is a no-op.
inline constexpr uint8_t scale8(uint8_t v, uint8_t s) {
  return static_cast<uint8_t>((static_cast<uint16_t>(v) * (static_cast<uint16_t>(s) + 1)) >> 8);
}

inline constexpr uint32_t from565(uint16_t c) {
  return pack(static_cast<uint8_t>(((c >> 11) & 0x1F) * 255 / 31),
              static_cast<uint8_t>(((c >> 5) & 0x3F) * 255 / 63),
              static_cast<uint8_t>((c & 0x1F) * 255 / 31));
}

// Per-channel mix from a to b, t 0..1. t outside that range extrapolates and the result is
// clamped per channel.
uint32_t lerp(uint32_t a, uint32_t b, float t);

// pct is how much of the original colour to keep: 100 returns c untouched, 0 returns pure grey.
uint32_t desaturate(uint32_t c, int pct);

uint32_t fromHex(const std::string& s, uint32_t fallback = kBlack);

bool tryFromHex(const std::string& s, uint32_t& out);

std::string toHex(uint32_t c);

uint32_t fromRgb(int r, int g, int b);

uint32_t fromHsv(int h, int s, int v);

uint32_t fromKelvin(int k);

}
}
