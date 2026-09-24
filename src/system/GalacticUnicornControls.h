#pragma once
#include "hal/GalacticUnicornDisplay.h"
#include "core/CoreEngine.h"

namespace awtrix {
class GalacticUnicornControls {
 public:
  void tick(CoreEngine& engine, const std::array<bool, 9>& pressed, int64_t nowMs) {
    using namespace galactic;
    for (size_t i = 3; i < Inputs.size(); ++i) {
      if (!buttons_[i].update(pressed[i], nowMs)) continue;
      const auto& s = engine.state().settings();
      Command c(CommandType::SetSettings);
      switch (Inputs[i].action) {
        case InputAction::Power:
          c.type = CommandType::SetDisplay;
          c.payload = engine.state().runtime().matrixOff ? "{\"power\":true}" : "{\"power\":false}";
          break;
        case InputAction::BrightnessUp:
        case InputAction::BrightnessDown:
          c.payload = "{\"autoBrightness\":false,\"brightness\":" +
              std::to_string(stepClamped(s.brightness,
                  Inputs[i].action == InputAction::BrightnessUp ? 10 : -10, 255)) + "}";
          break;
        case InputAction::VolumeUp:
        case InputAction::VolumeDown:
          // The onboard tone channel uses NG's buzzerVolume setting. No sink yet.
          c.payload = "{\"buzzerVolume\":" + std::to_string(stepClamped(s.buzzerVolume,
              Inputs[i].action == InputAction::VolumeUp ? 5 : -5, 100)) + "}";
          break;
        default: continue; // D deliberately unmapped; A/B/C use PeripheryService.
      }
      // Exactly the dispatcher used by HTTP/MQTT, with the same StateEvents.
      // Inline execution also makes simultaneous buttons compose deterministically.
      engine.execute(c);
    }
  }
 private:
  std::array<galactic::DebouncedButton, 9> buttons_{};
};
}
