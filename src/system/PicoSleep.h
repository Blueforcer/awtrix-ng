#pragma once
#include <cstdint>

namespace awtrix {
// Elapsed arithmetic avoids deadline overflow. A held sleep key must first be
// released, so the press that requested sleep cannot immediately wake it.
class PicoSleep {
 public:
  PicoSleep(uint64_t startMs, uint64_t durationMs, bool pressed)
      : start_(startMs), duration_(durationMs), released_(!pressed) {}
  bool wake(uint64_t nowMs, bool pressed) {
    if (!pressed) released_ = true;
    return nowMs - start_ >= duration_ || (released_ && pressed);
  }
 private:
  uint64_t start_, duration_;
  bool released_;
};
}
