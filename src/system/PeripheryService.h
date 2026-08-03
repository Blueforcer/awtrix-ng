#pragma once

#include <cstdint>

#include <functional>
#include <string>

#include "core/sensing/AutoBrightness.h"
#include "core/sensing/BatteryModel.h"
#include "core/sensing/MedianFilter.h"
#include "hal/IBoard.h"
#include "persistence/DeviceConfig.h"

namespace awtrix {

class CoreEngine;

class PeripheryService {
 public:
  void begin(CoreEngine& engine, IBoard& board, const DeviceConfig& cfg);
  void setUid(const std::string& uid) { uid_ = uid; }
  void setButtonHook(std::function<bool(int)> hook) { buttonHook_ = std::move(hook); }
  void tick(int64_t nowMs);

 private:
  LightConfig lightConfig() const;

  CoreEngine* engine_ = nullptr;
  IBoard* board_ = nullptr;
  const DeviceConfig* cfg_ = nullptr;
  std::function<bool(int)> buttonHook_;
  std::string uid_;
  // raw_ is the pin as sampled, stable_ the debounced value, prev_ the debounced value from the
  // previous tick that edges are detected against.
  ButtonState prev_{};
  ButtonState raw_{};
  ButtonState stable_{};
  int64_t rawChangeMs_[3] = {0, 0, 0};
  int64_t lastSelectEdgeMs_ = -100000;
  static constexpr long kDebounceMs = 35;
  static constexpr long kDoublePressMs = 300;
  int64_t lastSensorMs_ = -100000;
  int64_t lastLdrMs_ = -100000;
  MedianFilter<uint16_t, 5> ldrFilter_;
  BrightnessSmoother briSmoother_;
  bool wasAutoBrightness_ = false;
  MedianFilter<uint16_t, 5> batteryFilter_;
};

}
