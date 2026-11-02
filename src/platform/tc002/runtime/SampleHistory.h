#pragma once

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <vector>

namespace awtrix::tc002 {

constexpr uint32_t kDmaHalfSamples = 24;

constexpr uint32_t roundUpToDmaHalf(uint32_t samples) {
  return (samples + kDmaHalfSamples - 1) / kDmaHalfSamples * kDmaHalfSamples;
}

class SampleHistory {
 public:
  explicit SampleHistory(uint32_t capacity) : capacity_(capacity) {}

  void open() {
    ring_.assign(capacity_, 0);
    head_ = 0;
    held_ = 0;
    next_ = 0;
  }

  void release() {
    std::vector<int16_t>().swap(ring_);
    held_ = 0;
  }

  void append(uint32_t firstSample, const std::vector<int16_t>& samples) {
    if (firstSample != next_) held_ = 0;
    for (const int16_t sample : samples) {
      ring_[head_] = sample;
      head_ = (head_ + 1) % capacity_;
    }
    held_ = static_cast<uint32_t>(std::min<std::size_t>(std::size_t{held_} + samples.size(), capacity_));
    next_ = firstSample + static_cast<uint32_t>(samples.size());
  }

  uint32_t next() const { return next_; }

  uint32_t backTo(uint32_t sample) const {
    constexpr uint32_t kAheadOfStream = 0x80000000u;
    const uint32_t behind = next_ - sample;
    return behind < kAheadOfStream ? std::min(roundUpToDmaHalf(behind), held_) : 0;
  }

  std::vector<int16_t> read(uint32_t back, uint32_t count) const {
    std::vector<int16_t> samples(count);
    const std::size_t at = (head_ + capacity_ - back) % capacity_;
    for (uint32_t i = 0; i < count; ++i) samples[i] = ring_[(at + i) % capacity_];
    return samples;
  }

 private:
  uint32_t capacity_;
  std::vector<int16_t> ring_;
  std::size_t head_ = 0;
  uint32_t held_ = 0, next_ = 0;
};

}
