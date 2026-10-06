#pragma once
#include <cstdint>

namespace awtrix {
struct PlatformInputState {
  bool knob = false;
  // Clockwise detents, wrapping at the counter boundary.
  uint32_t knobTurns = 0;
};
}
