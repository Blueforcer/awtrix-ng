#pragma once

#include <cstdint>

#include "core/remote/RemoteWire.h"

namespace awtrix {
namespace remote {

// The buttons as the script host needs them: which are down now, and how often each went down,
// so a press shorter than a datagram's way still arrives.
class RemoteButtons {
 public:
  // Called by the button router for each button every frame.
  void update(int button, bool down) {
    if (button < 0 || button >= wire::kButtons) return;
    const uint8_t bit = static_cast<uint8_t>(1u << button);
    if (down && !(down_ & bit)) ++presses_[button];
    down_ = down ? static_cast<uint8_t>(down_ | bit) : static_cast<uint8_t>(down_ & ~bit);
  }
  uint8_t down() const { return down_; }
  const uint8_t (&presses() const)[wire::kButtons] { return presses_; }

 private:
  uint8_t down_ = 0;
  uint8_t presses_[wire::kButtons] = {0, 0, 0};
};

}
}
