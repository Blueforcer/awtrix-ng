#pragma once

#include <cstddef>
#include <cstdint>

namespace awtrix {
namespace audio {

// Turns a summed signal into samples. While the sum stays at or below kThreshold (-1 dBFS) it
// passes unchanged. A block that peaks above it is turned down to kThreshold: the gain falls
// across the block up to the first sample that would pass the threshold, so no sample does, and
// rises back to unity with a time constant of kReleaseMs once the overlap is over.
class Limiter {
 public:
  static constexpr int32_t kUnity = 32768;
  static constexpr int32_t kThreshold = 29204;
  static constexpr uint32_t kReleaseMs = 100;

  // blockFrames is the block size the caller usually hands over; its release step is computed once.
  Limiter(uint32_t rate, std::size_t blockFrames);
  void apply(const int32_t* sum, int16_t* out, std::size_t frames);
  void reset() { gain_ = kUnity; }
  int32_t gain() const { return gain_; }

 private:
  double perFrame_, blockRelease_;
  std::size_t blockFrames_;
  int32_t gain_ = kUnity;
};

}
}
