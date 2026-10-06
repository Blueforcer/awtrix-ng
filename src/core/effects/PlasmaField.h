#pragma once

#include <cstddef>
#include "core/render/FrameMemory.h"

namespace awtrix {
namespace fx {

// Scratch for the separable plasma effects: one value per column, per row and per diagonal, so the
// per-pixel loop becomes three array reads instead of three sine calls.
struct Axes {
  static constexpr int kMaxW = 128;
  static constexpr int kMaxH = 32;
  float* x = nullptr;
  float* y = nullptr;
  float* d = nullptr;
  float* storage = nullptr;
  std::size_t capacity = 0;
  void (*release)(void*) = nullptr;
  ~Axes() { if (storage) release(storage); }
  Axes() = default;
  Axes(const Axes&) = delete;
  Axes& operator=(const Axes&) = delete;

  bool fits(int w, int h) {
    if (w <= 0 || h <= 0 || w > kMaxW || h > kMaxH) return false;
    const std::size_t required = 2u * static_cast<std::size_t>(w + h);
    if (required > capacity) {
      const auto allocator = render::frameAllocator();
      auto* next = static_cast<float*>(allocator.allocate(required * sizeof(float)));
      if (!next) return false; // False without memory; the effect then computes every pixel.
      if (storage) release(storage);
      storage = next; release = allocator.release; capacity = required;
    }
    x = storage; y = x + w; d = y + h;
    return true;
  }
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
