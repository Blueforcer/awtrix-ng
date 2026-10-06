#include "platform/linux/script/KnobScripting.h"

#include <utility>

namespace awtrix::script {
KnobScripting::KnobScripting(std::function<int64_t()> clock, std::function<void()> restart)
    : clock_(std::move(clock)), restart_(std::move(restart)) {}

void KnobScripting::restart() {
  if (restart_) restart_();
}

void KnobScripting::forget(ScriptExtensionHost&, const std::string& app) {
  if (owner_ == app) owner_.clear();
}

bool KnobScripting::turn(const std::string& app, int direction) {
  const bool taken = host_ && host_->deliver(app, "on_knob", direction > 0 ? "right" : "left", nullptr);
  if (taken) restart();
  return taken;
}

bool KnobScripting::press(const std::string& app) {
  down_ = longSent_ = false;
  owner_.clear();
  if (!host_ || !host_->deliver(app, "on_knob", "press", nullptr)) return false;
  down_ = true;
  owner_ = app;
  pressedAt_ = clock_ ? clock_() : 0;
  restart();
  return true;
}

void KnobScripting::held(const std::string& app) {
  if (owner_ != app) owner_.clear();
  if (!down_ || longSent_ || owner_.empty()) return;
  const int64_t now = clock_ ? clock_() : 0;
  if (now - pressedAt_ < 500) return;
  longSent_ = true;
  host_->deliver(owner_, "on_knob", "long", nullptr);
  restart();
}

bool KnobScripting::release(const std::string& app) {
  if (!down_) return false;
  down_ = false;
  const std::string owner = std::move(owner_);
  owner_.clear();
  if (owner == app && !owner.empty()) {
    host_->deliver(owner, "on_knob", "release", nullptr);
    restart();
  }
  return true;
}
}
