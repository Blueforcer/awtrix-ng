#pragma once

#include <algorithm>
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
inline constexpr uint8_t scaleChannel8(uint8_t v, uint8_t s) {
  return static_cast<uint8_t>((static_cast<uint16_t>(v) * (static_cast<uint16_t>(s) + 1)) >> 8);
}

inline constexpr uint32_t scale8(uint32_t rgb, uint8_t level) {
  return pack(scaleChannel8(red(rgb), level), scaleChannel8(green(rgb), level), scaleChannel8(blue(rgb), level));
}

// Scales by a float level; truncates unless Rounding::Nearest is asked for.
enum class Rounding { Truncate, Nearest };
template <Rounding rounding = Rounding::Truncate>
inline uint32_t scale(uint32_t rgb, float level) {
  if (level >= 1.0f) return rgb;
  if (level <= 0.0f) return 0u;
  const auto channel = [&](int shift) {
    const float v = static_cast<float>((rgb >> shift) & 0xFFu) * level;
    if constexpr (rounding == Rounding::Nearest) return static_cast<uint32_t>(v + 0.5f) << shift;
    return static_cast<uint32_t>(v) << shift;
  };
  return channel(16) | channel(8) | channel(0);
}

// Mixes whole channel values; lerp below truncates the difference instead.
template <Rounding rounding = Rounding::Truncate>
inline uint32_t mix(uint32_t a, uint32_t b, float t) {
  t = t < 0.0f ? 0.0f : t > 1.0f ? 1.0f : t;
  const auto channel = [&](int shift) {
    const int from = (a >> shift) & 0xFFu, to = (b >> shift) & 0xFFu;
    const float value = from + (to - from) * t;
    if constexpr (rounding == Rounding::Nearest) return static_cast<uint32_t>(value + 0.5f) << shift;
    return static_cast<uint32_t>(value) << shift;
  };
  return channel(16) | channel(8) | channel(0);
}

inline constexpr uint32_t maximum(uint32_t a, uint32_t b) {
  return std::max(a & 0xFF0000u, b & 0xFF0000u) |
         std::max(a & 0x00FF00u, b & 0x00FF00u) |
         std::max(a & 0x0000FFu, b & 0x0000FFu);
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
