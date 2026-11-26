#pragma once

#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <functional>
#include <string>

namespace awtrix::hostd {

// The six digits a clock needs to pair with this host. A code lasts ten minutes and pairs one clock.
// Five wrong guesses in a row lock pairing for a minute, each further lock twice as long as the last
// up to an hour, until a clock pairs.
class PairingCode {
 public:
  static constexpr int64_t kValidMs = 10 * 60 * 1000;
  static constexpr int64_t kLockMs = 60 * 1000;
  static constexpr int64_t kMaxLockMs = 60 * 60 * 1000;
  static constexpr int kMaxFailures = 5;

  enum class Check { Ok, Wrong, Locked };
  using Random = std::function<uint32_t()>;

  explicit PairingCode(Random random) : random_(std::move(random)) {}

  // The code in force; a new one once the last expired or was used.
  const std::string& current(int64_t nowMs) {
    if (code_.empty() || nowMs - madeMs_ >= kValidMs) renew(nowMs);
    return code_;
  }
  int64_t validForMs(int64_t nowMs) {
    current(nowMs);
    return kValidMs - (nowMs - madeMs_);
  }
  // True once for every new code, so it can be logged.
  bool takeRenewed() {
    const bool renewed = renewed_;
    renewed_ = false;
    return renewed;
  }

  Check check(const std::string& code, int64_t nowMs) {
    if (nowMs < lockedUntilMs_) return Check::Locked;
    if (code == current(nowMs)) {
      failures_ = 0;
      nextLockMs_ = kLockMs;
      renew(nowMs);
      return Check::Ok;
    }
    if (++failures_ >= kMaxFailures) {
      failures_ = 0;
      lockedUntilMs_ = nowMs + nextLockMs_;
      nextLockMs_ = std::min(nextLockMs_ * 2, kMaxLockMs);
      renew(nowMs);
    }
    return Check::Wrong;
  }

 private:
  void renew(int64_t nowMs) {
    char text[8];
    std::snprintf(text, sizeof(text), "%06u", static_cast<unsigned>(random_() % 1000000u));
    code_ = text;
    madeMs_ = nowMs;
    renewed_ = true;
  }

  Random random_;
  std::string code_;
  int64_t madeMs_ = 0;
  int64_t lockedUntilMs_ = 0;
  int64_t nextLockMs_ = kLockMs;
  int failures_ = 0;
  bool renewed_ = false;
};

}
