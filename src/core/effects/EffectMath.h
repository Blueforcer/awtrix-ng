#pragma once

#include <algorithm>
#include <cmath>
#include <cstdint>

#include "core/render/Canvas.h"
#include "core/render/Color.h"

namespace awtrix {
namespace fx {

// Direction of (x, y) as a fraction of a full turn in 1/65536 steps, counter-clockwise from +x.
// Octant polynomial, within 0.3 degrees of atan2.
inline uint16_t angle16(float x, float y) {
  const float ax = std::fabs(x), ay = std::fabs(y);
  if (ax == 0.0f && ay == 0.0f) return 0;
  const bool steep = ay > ax;
  const float z = steep ? ax / ay : ay / ax;
  float turns = 0.125f * z + 0.0434f * z * (1.0f - z);
  if (steep) turns = 0.25f - turns;
  if (x < 0.0f) turns = 0.5f - turns;
  if (y < 0.0f) turns = 1.0f - turns;
  return static_cast<uint16_t>(static_cast<int32_t>(turns * 65536.0f));
}

// Position of a point bouncing between 0 and n - 1 after `step` moves of one pixel.
inline int bounce(int64_t step, int n) {
  if (n <= 1) return 0;
  const int64_t period = 2 * static_cast<int64_t>(n - 1);
  const int p = static_cast<int>(((step % period) + period) % period);
  return p < n ? p : static_cast<int>(period) - p;
}

inline uint32_t dim(uint32_t c, uint8_t level) {
  return color::scale8(c, level);
}

inline int nearest(float v) { return static_cast<int>(std::floor(v + 0.5f)); }

inline uint8_t level(float v) {
  return v <= 0.0f ? 0 : v >= 1.0f ? 255 : static_cast<uint8_t>(v * 255.0f);
}

// Per-channel maximum, so glows that overlap keep the brighter light instead of the later one.
inline void lighten(Canvas& c, int x, int y, uint32_t rgb) {
  if (x < 0 || y < 0 || x >= c.width() || y >= c.height()) return;
  const uint32_t o = c.getPixel(x, y);
  c.setPixel(x, y, color::maximum(o, rgb));
}

inline uint32_t hueColor(uint8_t index, int value) {
  return color::fromHsv(index * 360 / 256, 100, value);
}

// Whole-pixel magnification for pixel-art scenes: a 64x16 or 128x32 panel shows the same scene as
// a 32x8 one with every pixel drawn as a 2x2 or 4x4 block, centred in any leftover margin.
struct PixelGrid {
  int scale, left, top, width, height;
  explicit PixelGrid(const Canvas& c)
      : scale(std::max(1, std::min(c.height() / 8, c.width() / 32))),
        left(c.width() % scale / 2),
        top(c.height() % scale / 2),
        width(c.width() / scale),
        height(c.height() / scale) {}
  void fill(Canvas& c, int x, int y, int w, int h, uint32_t rgb) const {
    c.fillRect(left + x * scale, top + y * scale, w * scale, h * scale, rgb);
  }
};

struct Rng {
  uint32_t state;
  uint32_t next() {
    state ^= state << 13;
    state ^= state >> 17;
    state ^= state << 5;
    return state;
  }
  int below(int n) { return n > 0 ? static_cast<int>(next() % static_cast<uint32_t>(n)) : 0; }
  bool chance(int oneIn) { return below(oneIn) == 0; }
};

}
}
