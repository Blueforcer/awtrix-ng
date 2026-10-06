#include "platform/linux/host/HostButtonInput.h"

#include "core/CoreEngine.h"
#include "persistence/DeviceConfig.h"

namespace awtrix {

void HostButtonInput::begin(CoreEngine& engine, const DeviceConfig& cfg, input::IButtonMenu* menu) {
  engine_ = &engine;
  input::ButtonInput::begin(engine, cfg, menu);
}

void HostButtonInput::knob(bool pressed) {
  if (!engine_ || engine_->state().runtime().knob == pressed) return;
  engine_->state().runtime().knob = pressed;
  engine_->state().emit(StateEvent::ButtonsChanged);
}

void HostButtonInput::turn(int direction) {
  if (engine_) engine_->state().runtime().knobTurns += static_cast<uint32_t>(direction);
}

}
