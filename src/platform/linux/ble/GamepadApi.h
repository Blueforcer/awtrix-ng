#pragma once

#include <functional>
#include <string>

#include "platform/linux/ble/GamepadState.h"
#include "platform/linux/ble/RemoteGamepad.h"

namespace awtrix::ble {

// GET /api/v1/gamepad, POST /api/v1/gamepad/pair, DELETE /api/v1/gamepad/{id}, POST
// /api/v1/gamepad/remote and DELETE /api/v1/gamepad/remote/{session}. Without scripting there is no
// gamepad route; without Bluetooth there is nothing to pair or forget, and phones still play.
class GamepadApi {
 public:
  GamepadApi(const GamepadRegistry* bluetooth, RemoteGamepad* remote, std::function<GamepadPairResult()> pair,
             std::function<void(int)> forget)
      : bluetooth_(bluetooth), remote_(remote), pair_(std::move(pair)), forget_(std::move(forget)) {}

  // The HTTP status and its JSON body, or 0 for a path that is not the gamepad's.
  int handle(const std::string& method, const std::string& path, const std::string& request, std::string& body) const;

 private:
  int list(std::string& body) const;
  int pair(std::string& body) const;
  int startRemote(const std::string& request, std::string& body) const;
  int endRemote(const std::string& method, const std::string& path, std::string& body) const;
  int freePlayer() const;

  const GamepadRegistry* bluetooth_;
  RemoteGamepad* remote_;
  std::function<GamepadPairResult()> pair_;
  std::function<void(int)> forget_;
};

}
