#pragma once

#include <functional>
#include <vector>

#include "core/RuntimeState.h"
#include "core/Settings.h"

namespace awtrix {

enum class StateEvent : uint8_t {
  SettingsChanged,
  AppChanged,
  PowerChanged,
  BrightnessChanged,
  IndicatorChanged,
  ButtonsChanged,
  MoodlightChanged,
  NotificationChanged,
  RadioChanged
};

class StateStore {
 public:
  Settings& settings() { return settings_; }
  const Settings& settings() const { return settings_; }
  RuntimeState& runtime() { return runtime_; }
  const RuntimeState& runtime() const { return runtime_; }

  using Listener = std::function<void(StateEvent)>;
  void subscribe(Listener l) { listeners_.push_back(std::move(l)); }

  void emit(StateEvent e) {
    // Index-based on purpose: a listener may call subscribe() from inside a callback, and the
    // resulting push_back would invalidate the iterators of a range-based for loop.
    const size_t n = listeners_.size();
    for (size_t i = 0; i < n && i < listeners_.size(); ++i)
      if (listeners_[i]) listeners_[i](e);
  }

 private:
  Settings settings_;
  RuntimeState runtime_;
  std::vector<Listener> listeners_;
};

}
