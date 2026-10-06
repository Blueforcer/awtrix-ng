#pragma once

#include <cstdint>

#include <functional>
#include <string>

#include "core/input/ButtonInput.h"
#include "core/sensing/AutoBrightness.h"
#include "core/sensing/BatteryModel.h"
#include "core/sensing/MedianFilter.h"
#include "hal/IBoard.h"
#include "persistence/DeviceConfig.h"

namespace awtrix {

class CoreEngine;

class PeripheryService {
 public:
  void begin(CoreEngine& engine, IBoard& board, const DeviceConfig& cfg, input::IButtonMenu* menu);
  void setUid(const std::string& uid) { uid_ = uid; }
  void setButtonHook(input::ButtonRouter::ScriptHook hook) { buttons_.setButtonHook(std::move(hook)); }
  void tick(int64_t nowMs);

 private:
  CoreEngine* engine_ = nullptr;
  IBoard* board_ = nullptr;
  input::ButtonInput buttons_;
  std::string uid_;
  // raw_ is the pin sample; stable_ changes after debounce.
  ButtonState raw_{};
  ButtonState stable_{};
  int64_t rawChangeMs_[3] = {0, 0, 0};
  static constexpr long kDebounceMs = 35;
  int64_t lastSensorMs_ = -100000;
  int64_t lastLdrMs_ = -100000;
  MedianFilter<uint16_t, 5> ldrFilter_;
  MedianFilter<uint16_t, 5> batteryFilter_;
};

}
