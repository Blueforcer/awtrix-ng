#pragma once

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <vector>

namespace awtrix::speech {

// How much louder speech plays than the voice renders it: it shares the MP3 volume with music.
constexpr float kSpeechGain = 1.7782794100f;  // +5.0 dB

// kSpeechGain, then a look-ahead peak limiter: output lags lookahead() frames,
// stays within kCeiling and releases with kReleaseMs.
class SpeechLimiter {
 public:
  static constexpr float kCeiling = 29204.0f;  // -1 dBFS
  static constexpr float kLookaheadMs = 2.0f;
  static constexpr float kReleaseMs = 80.0f;

  explicit SpeechLimiter(uint32_t rate)
      : size_(std::max<std::size_t>(1, static_cast<std::size_t>(std::lround(rate * kLookaheadMs / 1000.0f))) + 1),
        release_(1.0f - std::exp(-1000.0f / (kReleaseMs * static_cast<float>(rate)))),
        delay_(size_ - 1, 0.0f),
        queue_(size_),
        window_(size_, 1.0f) {}

  // In place, the same number of frames, lookahead() frames late.
  void process(int16_t* samples, std::size_t count) {
    for (std::size_t i = 0; i < count; ++i) samples[i] = step(samples[i] * kSpeechGain);
  }

  // What is still held back, once the voice has ended; at most lookahead() frames.
  std::size_t flush(int16_t* out, std::size_t room) {
    const std::size_t frames = lookahead() < room ? lookahead() : room;
    for (std::size_t i = 0; i < frames; ++i) out[i] = step(0.0f);
    return frames;
  }

  std::size_t lookahead() const { return size_ - 1; }

 private:
  struct Need {
    uint64_t at = 0;
    float gain = 1.0f;
  };

  int16_t step(float in) {
    const float magnitude = std::fabs(in);
    const float need = magnitude > kCeiling ? kCeiling / magnitude : 1.0f;
    if (count_ && queue_[first_].at + size_ <= now_) {
      first_ = (first_ + 1) % size_;
      --count_;
    }
    while (count_ && queue_[(first_ + count_ - 1) % size_].gain >= need) --count_;
    queue_[(first_ + count_) % size_] = {now_++, need};
    ++count_;
    const float lowest = queue_[first_].gain;
    sum_ += lowest - window_[at_];
    window_[at_] = lowest;
    const float target = static_cast<float>(sum_ / static_cast<double>(size_));
    gain_ = target < gain_ ? target : gain_ + (target - gain_) * release_;
    const float out = delay_[held_] * gain_;
    delay_[held_] = in;
    held_ = (held_ + 1) % delay_.size();
    at_ = (at_ + 1) % size_;
    const float limited = out > kCeiling ? kCeiling : (out < -kCeiling ? -kCeiling : out);
    return static_cast<int16_t>(std::lround(limited));
  }

  const std::size_t size_;
  const float release_;
  std::vector<float> delay_;
  std::vector<Need> queue_;
  std::vector<float> window_;
  double sum_ = static_cast<double>(size_);
  float gain_ = 1.0f;
  uint64_t now_ = 0;
  std::size_t first_ = 0;
  std::size_t count_ = 0;
  std::size_t at_ = 0;
  std::size_t held_ = 0;
};

}
