#pragma once
#include <cstdint>

namespace awtrix::tc002::voice {
// One owner arbitrates knob gestures before either voice or quick settings sees
// them. A 500 ms hold starts voice; letting go afterwards ends nothing, so
// the user speaks with the knob released. A rotated, long or cancelled press
// never turns into a click on release.
class KnobGesture {
 public:
  enum class Action { None, Click, Start };
  static constexpr int64_t kHoldMs = 500;
  void down(int64_t now) {
    if (down_) return;
    down_ = true;
    rotated_ = false;
    started_ = false;
    at_ = now;
  }
  Action tick(int64_t now, bool enabled) {
    if (enabled && down_ && !rotated_ && !started_ && now - at_ >= kHoldMs) {
      started_ = true;
      return Action::Start;
    }
    return Action::None;
  }
  Action up(int64_t now) {
    if (!down_) return Action::None;
    const auto result = !started_ && !rotated_ && now - at_ < kHoldMs
                            ? Action::Click
                            : Action::None;
    down_ = started_ = false;
    return result;
  }
  bool turn() {
    if (started_) return false;
    if (down_) rotated_ = true;
    return true;
  }
  void cancel() {
    down_ = started_ = false;
    rotated_ = true;
  }

 private:
  bool down_ = false, rotated_ = false, started_ = false;
  int64_t at_ = 0;
};
}  // namespace awtrix::tc002::voice
