#pragma once

#include <array>
#include <string>

#include "platform/linux/ble/HidGamepad.h"

namespace awtrix::ble {

constexpr int kGamepadPlayers = 2;
enum class GamepadStatus : uint8_t { Unpaired, Pairing, Waiting, Connecting, Ready };
const char* gamepadStateName(GamepadStatus status);

// One player as a script reads it; the controls are all released unless the state is Ready.
struct PlayerInput {
  GamepadStatus state = GamepadStatus::Unpaired;
  HidControls controls;
};

// What scripts read, from any thread. `player` is 1..kGamepadPlayers.
class GamepadInput {
 public:
  virtual ~GamepadInput() = default;
  virtual PlayerInput input(int player) const = 0;
  virtual std::string name(int player) const = 0;
};

// A stored device slot; player is 0 while the device is not Ready.
struct GamepadDevice {
  GamepadStatus state = GamepadStatus::Unpaired;
  std::string name, address;
  int player = 0;
};
using GamepadDevices = std::array<GamepadDevice, kGamepadPlayers>;

// The Bluetooth gamepads as scripts and the HTTP API see them, from any thread.
class GamepadRegistry : public GamepadInput {
 public:
  virtual GamepadDevices devices() const = 0;
};

enum class GamepadPairStatus { Started, Full, Unavailable };
struct GamepadPairResult {
  GamepadPairStatus status = GamepadPairStatus::Unavailable;
  int id = 0;
};

}
