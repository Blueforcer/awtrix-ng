#pragma once

#include <string>
#include <vector>

#include "core/script/ScriptExtension.h"
#include "platform/linux/ble/GamepadState.h"

namespace awtrix::ble {

// `import gamepad` for scripts: reads the gamepad the firmware keeps, one value per call, so a
// game never holds on to a state that has moved on. Claims are shared between scripts per player.
class GamepadScripting : public script::ScriptExtension {
 public:
  explicit GamepadScripting(const GamepadInput& reader) : reader_(reader) {}

  std::vector<std::string> modules() const override { return {"gamepad"}; }
  void install(script::ScriptExtensionHost& host) override;

 private:
  static int state(bvm* vm);
  static int name(bvm* vm);
  static int buttons(bvm* vm);
  static int hat(bvm* vm);
  static int axis(bvm* vm);
  static int trigger(bvm* vm);
  static int direction(bvm* vm);
  static int playerIndex(bvm* vm);

  const GamepadInput& reader_;
};

}
