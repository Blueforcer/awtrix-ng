#include "core/render/ColorRamp.h"

#include <cmath>

#include "core/render/Color.h"

namespace awtrix {
namespace render {

uint32_t ColorRamp::at(float t) const {
  const float c = t <= 0.0f ? 0.0f : (t >= 1.0f ? 1.0f : t);
  return atIndex(static_cast<uint8_t>(c * 240.0f + 0.5f));
}

int ColorRamp::originAt(int64_t nowMs, int runWidthPx) const {
  if (speed == 0.0f) return 0;
  const int span = spanPx > 0 ? spanPx : (runWidthPx > 1 ? runWidthPx : 1);
  const double passes = static_cast<double>(speed) * static_cast<double>(nowMs) / 1000.0;
  return static_cast<int>((passes - std::floor(passes)) * span);
}

Palette paletteFromPositionedStops(const PaletteStop* stops, std::size_t n) {
  if (!stops || n == 0) return defaultPalette();
  if (n > 16) n = 16;
  if (n == 1) {
    Palette flat{};
    for (uint32_t& e : flat.entries) e = stops[0].color;
    return flat;
  }

  Palette p{};
  // Stop positions are 0..100 while the palette has 16 slots. Scaling entry i by 100 and each
  // stop by 15 puts both in the same fixed-point space, so no division is needed below.
  for (int i = 0; i < 16; ++i) {
    const int at = i * 100;
    if (at <= static_cast<int>(stops[0].pos) * 15) {
      p.entries[i] = stops[0].color;
      continue;
    }
    std::size_t lo = 0;
    while (lo + 1 < n && static_cast<int>(stops[lo + 1].pos) * 15 <= at) ++lo;
    if (lo + 1 >= n) {
      p.entries[i] = stops[n - 1].color;
      continue;
    }
    const int a = static_cast<int>(stops[lo].pos) * 15;
    const int b = static_cast<int>(stops[lo + 1].pos) * 15;
    const float t = b > a ? static_cast<float>(at - a) / static_cast<float>(b - a) : 0.0f;
    p.entries[i] = color::lerp(stops[lo].color, stops[lo + 1].color, t);
  }
  return p;
}

Palette paletteFromStops(const uint32_t* stops, std::size_t n) {
  if (!stops || n == 0) return defaultPalette();
  if (n > 16) n = 16;

  Palette p{};
  if (n == 1) {
    for (uint32_t& e : p.entries) e = stops[0];
    return p;
  }
  for (int i = 0; i < 16; ++i) {
    const float pos = static_cast<float>(i) * static_cast<float>(n - 1) / 15.0f;
    const std::size_t lo = static_cast<std::size_t>(pos);
    const std::size_t hi = lo + 1 < n ? lo + 1 : lo;
    p.entries[i] = color::lerp(stops[lo], stops[hi], pos - static_cast<float>(lo));
  }
  return p;
}

}
}
