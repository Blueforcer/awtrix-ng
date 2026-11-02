#pragma once

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>

namespace awtrix::tc002::voice {

inline double pcmRms(const int16_t* samples, std::size_t count) {
  double sum = 0, squares = 0;
  for (std::size_t i = 0; i < count; ++i) {
    const double sample = samples[i];
    sum += sample;
    squares += sample * sample;
  }
  const double mean = sum / count;
  return std::sqrt(std::max(0., squares / count - mean * mean));
}

}
