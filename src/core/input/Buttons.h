#pragma once

#include <cstdint>

namespace awtrix {

// The three front buttons by physical position, as the board samples them.
struct ButtonState {
  bool left = false;
  bool select = false;
  bool right = false;
};

namespace input {

// A button by role, after rotation and the swap setting: what scripts and navigation see.
enum class Button : uint8_t { Left = 0, Select = 1, Right = 2 };
constexpr int kButtonCount = 3;
constexpr int index(Button b) { return static_cast<int>(b); }

}

}
