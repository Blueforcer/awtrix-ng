#pragma once

#include <cmath>
#include <cstdint>

namespace awtrix {

namespace noise {

inline uint32_t& seedRef() {
  static uint32_t s = 0;
  return s;
}
// Mixed into every hash, so the random-looking effects don't lay down the identical pattern on
// every power-up.
inline void reseed(uint32_t entropy) { seedRef() = entropy; }

inline uint32_t hash(uint32_t v) {
  v ^= v >> 16;
  v *= 0x7FEB352Du;
  v ^= v >> 15;
  v *= 0x846CA68Bu;
  v ^= v >> 16;
  return v;
}

inline uint32_t hash2(uint32_t a, uint32_t salt) {
  return hash(a * 0x9E3779B9u + salt + seedRef());
}

// Smooth value noise in 0..1 with one lattice cell per unit, read along a row of constant y and
// z. Corners are hashed once per cell.
class NoiseRow {
 public:
  NoiseRow(float y, float z, uint32_t salt) : salt_(salt) {
    const float fy = std::floor(y), fz = std::floor(z);
    yi_ = static_cast<int32_t>(fy);
    zi_ = static_cast<int32_t>(fz);
    sy_ = smooth(y - fy);
    sz_ = smooth(z - fz);
  }

  float at(float x) {
    const float fx = std::floor(x);
    const int32_t xi = static_cast<int32_t>(fx);
    if (!primed_ || xi != xi_) {
      left_ = xi == xi_ + 1 && primed_ ? right_ : edge(xi);
      right_ = edge(xi + 1);
      xi_ = xi;
      primed_ = true;
    }
    return left_ + (right_ - left_) * smooth(x - fx);
  }

 private:
  static float smooth(float t) { return t * t * (3.0f - 2.0f * t); }

  float corner(int32_t x, int32_t y, int32_t z) const {
    const uint32_t h = hash(static_cast<uint32_t>(x) * 0x8DA6B343u ^ static_cast<uint32_t>(y) * 0xD8163841u ^
                            static_cast<uint32_t>(z) * 0xCB1AB31Fu ^ salt_ ^ seedRef());
    return static_cast<float>(h >> 8) * (1.0f / 16777216.0f);
  }

  float edge(int32_t x) const {
    const float a = corner(x, yi_, zi_), b = corner(x, yi_ + 1, zi_);
    const float c = corner(x, yi_, zi_ + 1), d = corner(x, yi_ + 1, zi_ + 1);
    const float near = a + (b - a) * sy_, far = c + (d - c) * sy_;
    return near + (far - near) * sz_;
  }

  uint32_t salt_;
  int32_t xi_ = 0, yi_, zi_;
  float sy_, sz_, left_ = 0.0f, right_ = 0.0f;
  bool primed_ = false;
};

}

}
