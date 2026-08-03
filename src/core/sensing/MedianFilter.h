#pragma once

#include <algorithm>
#include <cstddef>

namespace awtrix {

template <typename T, std::size_t N>
class MedianFilter {
 public:
  T push(T value) {
    // The first sample fills the whole window, so the filter returns something sensible from the
    // very first call instead of dragging a run of zeros behind it.
    if (!primed_) {
      std::fill(samples_, samples_ + N, value);
      primed_ = true;
    } else {
      samples_[idx_] = value;
      idx_ = (idx_ + 1) % N;
    }
    T sorted[N];
    std::copy(samples_, samples_ + N, sorted);
    std::sort(sorted, sorted + N);
    return sorted[N / 2];
  }

  void reset() {
    idx_ = 0;
    primed_ = false;
  }

 private:
  T samples_[N] = {};
  std::size_t idx_ = 0;
  bool primed_ = false;
};

}
