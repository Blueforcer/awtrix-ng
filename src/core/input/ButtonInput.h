#pragma once

#include <utility>

#include "core/input/ButtonRouter.h"
#include "persistence/DeviceConfig.h"

namespace awtrix::input {

// Receives debounced button states and reports physical edges after routing them.
class ButtonInput {
  struct IgnoreEdge { void operator()(int, bool) const {} };

 public:
  void begin(CoreEngine& engine, const DeviceConfig& config, IButtonMenu* menu) {
    config_ = &config;
    router_.begin(engine, menu);
  }
  void setButtonHook(ButtonRouter::ScriptHook hook) { router_.setScriptHook(std::move(hook)); }
  const DeviceConfig& config() const { return *config_; }
  const ButtonState& state() const { return state_; }

  template <class Edge = IgnoreEdge>
  void setState(const ButtonState& current, int64_t nowMs, Edge edge = {}) {
    if (!config_) return;
    router_.update(current, config_->rotate != config_->swapButtons, nowMs);
    if (current.left != state_.left) edge(0, current.left);
    if (current.select != state_.select) edge(1, current.select);
    if (current.right != state_.right) edge(2, current.right);
    state_ = current;
  }

  template <class Edge = IgnoreEdge>
  void transition(int button, bool pressed, int64_t nowMs, Edge edge = {}) {
    ButtonState next = state_;
    switch (button) {
      case 0: next.left = pressed; break;
      case 1: next.select = pressed; break;
      case 2: next.right = pressed; break;
      default: return;
    }
    setState(next, nowMs, std::move(edge));
  }

  void tickHeld(int64_t nowMs) { setState(state_, nowMs); }

 private:
  const DeviceConfig* config_ = nullptr;
  ButtonRouter router_;
  ButtonState state_{};
};

}
