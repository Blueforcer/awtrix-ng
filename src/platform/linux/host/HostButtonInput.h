#pragma once

#include <cstdint>
#include <utility>

#include "core/input/ButtonInput.h"

namespace awtrix {

class CoreEngine;
struct DeviceConfig;

// Receives ordered, stable input; raw sampling and debounce belong to each adapter.
class HostButtonInput : public input::ButtonInput {
 public:
  void begin(CoreEngine& engine, const DeviceConfig& cfg, input::IButtonMenu* menu);
  // The knob only reports; what it does on the device is the platform's business.
  void knob(bool pressed);
  void turn(int direction);

 private:
  CoreEngine* engine_ = nullptr;
};

}
