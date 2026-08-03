#include "core/notify/NotificationManager.h"

namespace awtrix {

// A non-stacking notification replaces whatever is on screen and restarts its timer; a stacking
// one queues behind it and is refused once the queue is full.
bool NotificationManager::push(const AppSpec& n, int64_t nowMs) {
  if (!n.stack) {
    if (!queue_.empty()) {
      queue_.front() = n;
    } else {
      queue_.push_back(n);
    }
    currentStartMs_ = nowMs;
    ++generation_;
    return true;
  }
  if (queue_.size() >= capacity_) return false;
  const bool wasEmpty = queue_.empty();
  queue_.push_back(n);
  if (wasEmpty) {
    currentStartMs_ = nowMs;
    ++generation_;
  }
  return true;
}

void NotificationManager::dismiss(int64_t nowMs) {
  if (queue_.empty()) return;
  queue_.pop_front();
  currentStartMs_ = nowMs;
  ++generation_;
}

bool NotificationManager::dismissNamed(const std::string& name, int64_t nowMs) {
  if (name.empty()) return false;
  for (auto it = queue_.begin(); it != queue_.end(); ++it) {
    if (it->name != name) continue;
    const bool wasFront = (it == queue_.begin());
    queue_.erase(it);
    if (wasFront) {
      currentStartMs_ = nowMs;
      ++generation_;
    }
    return true;
  }
  return false;
}

void NotificationManager::update(int64_t nowMs, long defaultDurationMs, bool hold,
                                 bool passesDone) {
  if (queue_.empty()) return;
  const AppSpec& n = queue_.front();
  if (n.hold || hold) return;
  // Before the text has finished its scroll passes a notification without a duration falls back
  // to the configured default; once the passes are done, no duration means "go now".
  const long duration = passesDone ? n.durationMs
                                   : (n.durationMs > 0 ? n.durationMs : defaultDurationMs);
  if (nowMs - currentStartMs_ >= duration) {
    queue_.pop_front();
    currentStartMs_ = nowMs;
    ++generation_;
  }
}

}
