#pragma once

#include <cstdint>
#include <string>

#include "core/StateStore.h"

namespace awtrix {

// Decides when each MQTT state topic is worth republishing: settings, buttons and radio go out on
// change, the stats blob on a timer that a change can cut short.
class StatePublishCadence {
 public:
  // Floor between two change-driven state pushes, so a burst of indicator writes cannot flood the
  // broker.
  static constexpr int64_t kMinForcedGapMs = 250;

  void configure(long statsIntervalMs) {
    statsIntervalMs_ = statsIntervalMs < 1000 ? 1000 : static_cast<int64_t>(statsIntervalMs);
  }

  void onEvent(StateEvent e) {
    if (e == StateEvent::SettingsChanged) settingsDirty_ = true;
    if (e == StateEvent::PowerChanged || e == StateEvent::IndicatorChanged) statesDirty_ = true;
    if (e == StateEvent::ButtonsChanged) buttonsDirty_ = true;
    if (e == StateEvent::RadioChanged) radioDirty_ = true;
  }

  // A fresh broker session republishes everything. Clearing lastApp_ matters: appDue would
  // otherwise hold the app topic back as unchanged.
  void onConnect() {
    settingsDirty_ = true;
    radioDirty_ = true;
    buttonsDirty_ = true;
    lastApp_.clear();
  }

  bool appDue(const std::string& app) {
    if (app == lastApp_) return false;
    lastApp_ = app;
    return true;
  }

  bool radioDue() {
    const bool due = radioDirty_;
    radioDirty_ = false;
    return due;
  }

  bool settingsDue() {
    const bool due = settingsDirty_;
    settingsDirty_ = false;
    return due;
  }

  bool buttonsDue() {
    const bool due = buttonsDirty_;
    buttonsDirty_ = false;
    return due;
  }

  bool stateDue(int64_t nowMs) {
    const bool forced = statesDirty_ && (nowMs - lastStateMs_ >= kMinForcedGapMs);
    if (!forced && nowMs - lastStateMs_ < statsIntervalMs_) return false;
    lastStateMs_ = nowMs;
    statesDirty_ = false;
    return true;
  }

 private:
  std::string lastApp_;
  int64_t statsIntervalMs_ = 10000;
  int64_t lastStateMs_ = 0;
  bool settingsDirty_ = true;
  bool statesDirty_ = false;
  bool buttonsDirty_ = true;
  bool radioDirty_ = true;
};

}
