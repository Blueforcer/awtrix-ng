#pragma once

#include <cstddef>

namespace awtrix {
namespace fx {

// Scratch for the separable plasma effects: one value per column, per row and per diagonal, so the
// per-pixel loop becomes three array reads instead of three sine calls.
struct Axes {
  static constexpr int kMaxW = 128;
  static constexpr int kMaxH = 16;
  float x[kMaxW];
  float y[kMaxH];
  float d[kMaxW + kMaxH];

  bool fits(int w, int h) const { return w <= kMaxW && h <= kMaxH; }
};

// One shared buffer for all of them; only a single effect renders at any moment.
inline Axes& axes() {
  static Axes a;
  return a;
}

// d is indexed by x + y, so it needs w + h - 1 entries to cover every diagonal.
template <typename FX, typename FY, typename FD>
void sampleAxes(Axes& a, int w, int h, FX&& wx, FY&& wy, FD&& wd) {
  for (int i = 0; i < w; ++i) a.x[i] = wx(i);
  for (int i = 0; i < h; ++i) a.y[i] = wy(i);
  for (int i = 0; i < w + h - 1; ++i) a.d[i] = wd(i);
}

}
}
