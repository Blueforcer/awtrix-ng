#pragma once

#include <algorithm>
#include <cstdint>

namespace awtrix::remote {

class FramePacer {
 public:
  explicit FramePacer(int64_t periodMs) : periodMs_(periodMs > 0 ? periodMs : 1) {}

  void start(int64_t nowMs) {
    tickMs_ = nowMs;
    skipped_ = 0;
  }
  int64_t tickMs() const { return tickMs_; }
  int64_t skipped() const { return skipped_; }

  int64_t advance(int64_t nowMs) {
    const int64_t steps = std::max<int64_t>(1, (nowMs - tickMs_) / periodMs_);
    skipped_ += steps - 1;
    tickMs_ += steps * periodMs_;
    return tickMs_;
  }

 private:
  int64_t periodMs_;
  int64_t tickMs_ = 0;
  int64_t skipped_ = 0;
};

}
