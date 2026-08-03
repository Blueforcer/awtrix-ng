#include "core/apps/AppHost.h"

namespace awtrix {

namespace {
const std::string kEmpty;
}

int AppHost::indexOf(const std::string& id) const {
  if (id.empty()) return -1;
  for (int i = 0; i < static_cast<int>(apps_.size()); ++i) {
    if (apps_[i] == id) return i;
  }
  return -1;
}

// Keeps the showing app across a list change by id rather than index, and drops a running
// transition whose target has disappeared.
void AppHost::setApps(const std::vector<std::string>& ids) {
  if (ids == apps_) return;

  const std::string keep =
      (current_ >= 0 && current_ < static_cast<int>(apps_.size())) ? apps_[current_] : std::string();
  const std::string keepTarget =
      (phase_ == AppPhase::InTransition && target_ >= 0 && target_ < static_cast<int>(apps_.size()))
          ? apps_[target_]
          : std::string();

  apps_ = ids;
  const int found = indexOf(keep);
  if (found >= 0) {
    current_ = found;
  } else if (current_ >= static_cast<int>(apps_.size())) {
    current_ = apps_.empty() ? 0 : static_cast<int>(apps_.size()) - 1;
  }

  const int target = found >= 0 ? indexOf(keepTarget) : -1;
  if (target >= 0 && target != current_) {
    target_ = target;
  } else {
    phase_ = AppPhase::Fixed;
    target_ = -1;
  }
}

const std::string& AppHost::currentId() const {
  if (current_ >= 0 && current_ < static_cast<int>(apps_.size())) return apps_[current_];
  return kEmpty;
}

// Next index in direction dir that the gate lets through, or -1 when nothing else can be shown.
int AppHost::pick(int from, int dir) const {
  const int n = static_cast<int>(apps_.size());
  for (int step = 1; step < n; ++step) {
    const int i = ((from + dir * step) % n + n) % n;
    if (!gate_ || gate_(apps_[i])) return i;
  }
  return -1;
}

void AppHost::startPhase(AppPhase phase, int64_t nowMs) {
  phase_ = phase;
  phaseStartMs_ = nowMs;
  clockStarted_ = true;
}

void AppHost::settle(int64_t nowMs) {
  if (phase_ != AppPhase::InTransition) return;
  if (target_ >= 0 && target_ < static_cast<int>(apps_.size())) current_ = target_;
  target_ = -1;
  direction_ = 1;
  startPhase(AppPhase::Fixed, nowMs);
}

void AppHost::beginTransition(int dir, int64_t nowMs) {
  if (apps_.size() < 2) return;
  settle(nowMs);
  const int t = pick(current_, dir);
  if (t < 0) {
    startPhase(AppPhase::Fixed, nowMs);
    return;
  }
  direction_ = dir;
  target_ = t;
  startPhase(AppPhase::InTransition, nowMs);
}

void AppHost::tick(int64_t nowMs, long appDurationMs, long transitionDurationMs, bool autoTransition) {
  if (apps_.empty()) return;
  // The first tick starts the clock, so boot time before the first frame is not counted against
  // the app's turn on screen.
  if (!clockStarted_) startPhase(phase_, nowMs);
  if (phase_ == AppPhase::Fixed) {
    if (autoTransition && apps_.size() >= 2 && (nowMs - phaseStartMs_) >= appDurationMs) {
      beginTransition(direction_, nowMs);
    }
  } else {
    if ((nowMs - phaseStartMs_) >= transitionDurationMs) settle(nowMs);
  }
}

void AppHost::next(int64_t nowMs) { beginTransition(1, nowMs); }
void AppHost::previous(int64_t nowMs) { beginTransition(-1, nowMs); }

bool AppHost::switchTo(const std::string& id, int64_t nowMs) {
  for (int i = 0; i < static_cast<int>(apps_.size()); ++i) {
    if (apps_[i] == id) {
      current_ = i;
      target_ = -1;
      startPhase(AppPhase::Fixed, nowMs);
      return true;
    }
  }
  return false;
}

bool AppHost::transitionTo(const std::string& id, int64_t nowMs) {
  const int i = indexOf(id);
  if (i < 0) return false;
  if (phase_ == AppPhase::InTransition && i == target_) return true;
  settle(nowMs);
  if (i == current_) return true;
  direction_ = 1;
  target_ = i;
  startPhase(AppPhase::InTransition, nowMs);
  return true;
}

}
