#pragma once

#include <cstddef>
#include <cstdint>

namespace awtrix {
namespace audio {

// value times a Q15 gain, where 32768 is unity.
inline int32_t scaled(int32_t value, int32_t gain) {
  return static_cast<int32_t>((static_cast<int64_t>(value) * gain) >> 15);
}

// A Q15 gain moving from one value to another over a block, one step per frame.
// Quotient/remainder stepping is exactly trunc(delta * i / frames), including
// descending ramps. Unlike a rounded Q16 step it cannot accumulate a gain error.
class GainRamp {
 public:
  GainRamp(int32_t from, int32_t to, std::size_t frames) : gain_(from), frames_(frames) {
    if (!frames || from == to) return;
    const int64_t delta = static_cast<int64_t>(to) - from;
    direction_ = delta < 0 ? -1 : 1;
    const auto distance = static_cast<uint64_t>(delta < 0 ? -delta : delta);
    step_ = static_cast<int32_t>(distance / frames) * direction_;
    remainder_ = static_cast<std::size_t>(distance % frames);
  }
  int32_t next() {
    const int32_t result = gain_;
    gain_ += step_;
    error_ += remainder_;
    if (remainder_ && error_ >= frames_) {
      error_ -= frames_;
      gain_ += direction_;
    }
    return result;
  }

 private:
  int32_t gain_, step_ = 0, direction_ = 0;
  std::size_t frames_, remainder_ = 0, error_ = 0;
};

}
}
