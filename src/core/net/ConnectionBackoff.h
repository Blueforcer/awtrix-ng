#pragma once

#include <cstdint>

namespace awtrix {
namespace net {

// Reconnect timing: 5 s doubling to a 60 s ceiling, with jitter so a fleet coming back after a
// router reboot does not retry in lockstep. Caller arms it once and polls due().
class ConnectionBackoff {
 public:
  static constexpr uint32_t kFirstRetryMs = 5000;
  static constexpr uint32_t kMaxRetryMs = 60000;
  static constexpr uint32_t kJitterPercent = 20;

  void onSuccess() { reset(); }

  uint32_t onFailure(uint32_t entropy) {
    if (attempts_ < UINT16_MAX) ++attempts_;
    delayMs_ = jitter(base_, entropy);
    base_ = base_ > kMaxRetryMs / 2 ? kMaxRetryMs : base_ * 2;
    return delayMs_;
  }

  void arm(uint32_t nowMs) {
    armedAtMs_ = nowMs;
    armed_ = true;
  }

  // Unsigned subtraction, so both this and retryInMs() stay correct across millis() rollover.
  bool due(uint32_t nowMs) const { return !armed_ || (nowMs - armedAtMs_) >= delayMs_; }

  uint32_t retryInMs(uint32_t nowMs) const {
    if (!armed_) return 0;
    const uint32_t elapsed = nowMs - armedAtMs_;
    return elapsed >= delayMs_ ? 0 : delayMs_ - elapsed;
  }

  uint32_t delayMs() const { return delayMs_; }
  uint16_t attempts() const { return attempts_; }

  void reset() {
    base_ = kFirstRetryMs;
    delayMs_ = kFirstRetryMs;
    attempts_ = 0;
    armed_ = false;
  }

 private:
  // Xorshift over whatever entropy the caller has, subtracting up to kJitterPercent. Only ever
  // subtracts, so the 60 s ceiling still holds.
  static uint32_t jitter(uint32_t ms, uint32_t entropy) {
    uint32_t x = entropy ? entropy : 0x9E3779B9u;
    x ^= x << 13;
    x ^= x >> 17;
    x ^= x << 5;
    const uint32_t span = ms / 100u * kJitterPercent;
    return span ? ms - (x % span) : ms;
  }

  uint32_t base_ = kFirstRetryMs;
  uint32_t delayMs_ = kFirstRetryMs;
  uint32_t armedAtMs_ = 0;
  uint16_t attempts_ = 0;
  bool armed_ = false;
};

}
}
