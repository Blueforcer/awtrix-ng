#pragma once

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

}

}
