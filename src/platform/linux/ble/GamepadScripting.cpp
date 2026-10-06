#include "platform/linux/ble/GamepadScripting.h"

#include <cstring>

#include "berry.h"
#include "core/script/BerryVM.h"

namespace awtrix::ble {
namespace {

constexpr const char* kSource = R"BERRY(
var m = module('gamepad')
var names = {'A': 0, 'B': 1, 'X': 3, 'Y': 4, 'L1': 6, 'R1': 7, 'L2': 8, 'R2': 9,
             'SELECT': 10, 'START': 11, 'HOME': 12, 'L3': 13, 'R3': 14}

m.state = _gamepad_state
m.ready = def (player) return _gamepad_state(player) == 'ready' end
m.name = _gamepad_name
m.buttons = _gamepad_buttons
m.hat = _gamepad_hat
m.axis = _gamepad_axis
m.trigger = _gamepad_trigger

m.bit = def (button)
  if type(button) == 'int' return 1 << button end
  var n = names.find(button)
  if n == nil raise 'value_error', 'no button named ' + str(button) end
  return 1 << n
end
m.down = def (button, player) return (_gamepad_buttons(player) & m.bit(button)) != 0 end
m.pressed = def (before, button, player) return (_gamepad_buttons(player) & ~before & m.bit(button)) != 0 end

m.dir = _gamepad_dir

var holders = [nil, nil]
var since = [0, 0]
m.claim = def (who, player)
  var p = _gamepad_player(player)
  holders[p] = who
  since[p] = now_ms()
end
m.release = def (who, player)
  var p = _gamepad_player(player)
  if holders[p] == who holders[p] = nil end
end
m.mine = def (who, player)
  var p = _gamepad_player(player)
  return holders[p] == nil || holders[p] == who || now_ms() - since[p] > 1000
end

return m
)BERRY";

const GamepadInput& readerOf(bvm* vm) { return *static_cast<const GamepadInput*>(script::BerryVM::nativeSelf(vm)); }

int playerOf(bvm* vm, int at = 1) {
  if (be_top(vm) < at || be_isnil(vm, at)) return 1;
  if (!be_isint(vm, at) || be_toint(vm, at) < 1 || be_toint(vm, at) > kGamepadPlayers)
    be_raise(vm, "value_error", "player must be 1..2");
  return static_cast<int>(be_toint(vm, at));
}

// The index of `which` in names, or raises value_error.
int pick(bvm* vm, const char* const* names, int count, const char* what) {
  const char* which = be_top(vm) >= 1 && be_isstring(vm, 1) ? be_tostring(vm, 1) : "";
  for (int i = 0; i < count; ++i)
    if (std::strcmp(which, names[i]) == 0) return i;
  be_raise(vm, "value_error", what);
  return 0;
}

}

void GamepadScripting::install(script::ScriptExtensionHost& host) {
  void* self = const_cast<GamepadInput*>(&reader_);
  host.defineNative("_gamepad_state", &GamepadScripting::state, self);
  host.defineNative("_gamepad_name", &GamepadScripting::name, self);
  host.defineNative("_gamepad_buttons", &GamepadScripting::buttons, self);
  host.defineNative("_gamepad_hat", &GamepadScripting::hat, self);
  host.defineNative("_gamepad_axis", &GamepadScripting::axis, self);
  host.defineNative("_gamepad_trigger", &GamepadScripting::trigger, self);
  host.defineNative("_gamepad_dir", &GamepadScripting::direction, self);
  host.defineNative("_gamepad_player", &GamepadScripting::playerIndex, self);
  host.defineModule("gamepad", kSource);
}

int GamepadScripting::state(bvm* vm) {
  be_pushstring(vm, gamepadStateName(readerOf(vm).input(playerOf(vm)).state));
  be_return(vm);
}

int GamepadScripting::name(bvm* vm) {
  be_pushstring(vm, readerOf(vm).name(playerOf(vm)).c_str());
  be_return(vm);
}

int GamepadScripting::buttons(bvm* vm) {
  be_pushint(vm, static_cast<bint>(readerOf(vm).input(playerOf(vm)).controls.buttons));
  be_return(vm);
}

int GamepadScripting::hat(bvm* vm) {
  be_pushint(vm, readerOf(vm).input(playerOf(vm)).controls.hat);
  be_return(vm);
}

// -100 to 100, up and left negative.
int GamepadScripting::axis(bvm* vm) {
  static const char* const kAxes[] = {"lx", "ly", "rx", "ry"};
  const int k = pick(vm, kAxes, 4, "the axes are lx, ly, rx and ry");
  be_pushint(vm, (readerOf(vm).input(playerOf(vm, 2)).controls.axes[k] - 128) * 100 / 127);
  be_return(vm);
}

// 0 released to 100 pulled.
int GamepadScripting::trigger(bvm* vm) {
  static const char* const kTriggers[] = {"lt", "rt"};
  const int k = pick(vm, kTriggers, 2, "the triggers are lt and rt");
  be_pushint(vm, readerOf(vm).input(playerOf(vm, 2)).controls.triggers[k] * 100 / 255);
  be_return(vm);
}

int GamepadScripting::playerIndex(bvm* vm) {
  be_pushint(vm, playerOf(vm) - 1);
  be_return(vm);
}

int GamepadScripting::direction(bvm* vm) {
  const HidControls controls = readerOf(vm).input(playerOf(vm)).controls;
  int dx = 0, dy = 0;
  if (controls.hat >= 0 && controls.hat < 8) {
    static const int x[] = {0, 1, 1, 1, 0, -1, -1, -1}, y[] = {-1, -1, 0, 1, 1, 1, 0, -1};
    dx = x[controls.hat];
    dy = y[controls.hat];
  } else {
    const int x = (controls.axes[0] - 128) * 100 / 127, y = (controls.axes[1] - 128) * 100 / 127;
    dx = x > 33 ? 1 : x < -33 ? -1 : 0;
    dy = y > 33 ? 1 : y < -33 ? -1 : 0;
  }
  if (!dx && !dy) { be_pushnil(vm); be_return(vm); }
  be_newobject(vm, "list");
  for (int value : {dx, dy}) {
    be_pushint(vm, value);
    be_data_push(vm, -2);
    be_pop(vm, 1);
  }
  be_pop(vm, 1);
  be_return(vm);
}

}
