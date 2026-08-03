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
    for (auto& l : listeners_)
      if (l) l(e);
  }

 private:
  Settings settings_;
  RuntimeState runtime_;
  std::vector<Listener> listeners_;
};

}
