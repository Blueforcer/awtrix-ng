#pragma once

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <vector>

namespace awtrix::speech {

// A fixed number of samples, first in, first out. It does not lock; SpeechSource does.
class SpeechRing {
 public:
  explicit SpeechRing(std::size_t capacity) : samples_(capacity) {}

  std::size_t size() const { return size_; }
  std::size_t room() const { return samples_.size() - size_; }

  // Appends as many as fit and returns how many.
  std::size_t write(const int16_t* in, std::size_t count) {
    count = std::min(count, room());
    const std::size_t at = (head_ + size_) % samples_.size();
    const std::size_t first = std::min(count, samples_.size() - at);
    std::copy_n(in, first, samples_.begin() + static_cast<std::ptrdiff_t>(at));
    std::copy_n(in + first, count - first, samples_.begin());
    size_ += count;
    return count;
  }

  // Takes up to max of the oldest and returns how many.
  std::size_t read(int16_t* out, std::size_t max) {
    const std::size_t count = std::min(max, size_);
    const std::size_t first = std::min(count, samples_.size() - head_);
    std::copy_n(samples_.begin() + static_cast<std::ptrdiff_t>(head_), first, out);
    std::copy_n(samples_.begin(), count - first, out + first);
    head_ = (head_ + count) % samples_.size();
    size_ -= count;
    return count;
  }

 private:
  std::vector<int16_t> samples_;
  std::size_t head_ = 0;
  std::size_t size_ = 0;
};

}
