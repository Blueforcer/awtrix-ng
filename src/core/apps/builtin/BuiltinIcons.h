#pragma once

#include <cstdint>

#include "core/render/Canvas.h"
#include "core/render/Color.h"

namespace awtrix {
namespace builtinicon {

inline constexpr int kSize = 8;

// 8x8 icons in RGB565, row-major. A 0 entry is transparent and leaves the pixel untouched.
inline constexpr uint16_t kThermometer[64] = {
    0, 65535, 65535, 65535, 0, 0, 0, 0, 0, 65535, 0, 65535, 0, 0, 0, 0,
    0, 65535, 63488, 65535, 0, 0, 0, 0, 0, 65535, 63488, 65535, 0, 0, 0, 0,
    0, 65535, 63488, 65535, 0, 0, 0, 0, 65535, 63488, 63488, 63488, 65535, 0, 0, 0,
    65535, 63488, 63488, 63488, 65535, 0, 0, 0, 0, 65535, 65535, 65535, 0, 0, 0, 0};

inline constexpr uint16_t kDrop[64] = {
    0,     0,     0,     19967, 0,     0,    0, 0,
    0,     0,     0,     19967, 0,     0,    0, 0,
    0,     0,     57247, 57247, 19967, 0,    0, 0,
    0,     0,     57247, 19967, 19967, 0,    0, 0,
    0,     57247, 19967, 19967, 19967, 1049, 0, 0,
    0,     19967, 19967, 19967, 19967, 1049, 0, 0,
    0,     19967, 19967, 19967, 1049,  1049, 0, 0,
    0,     0,     1049,  1049,  1049,  0,    0, 0};

inline void draw(Canvas& c, const uint16_t (&icon)[64], int x0, int y0) {
  for (int y = 0; y < 8; ++y)
    for (int x = 0; x < 8; ++x) {
      const uint16_t v = icon[y * 8 + x];
      if (v) c.setPixel(x0 + x, y0 + y, color::from565(v));
    }
}

}
}
