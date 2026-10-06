#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <mutex>
#include <string>
#include <vector>

#include "platform/linux/ble/GamepadState.h"
#include "transport/net/UdpSocket.h"

namespace awtrix::ble {

// Phones standing in for gamepads, at most one per player. A phone asks for a session over HTTP,
// gets a session number and a token, and sends its controls as UDP datagrams to kPort (the layout
// is in docs/reference/http.md). A session ends when kTimeoutMs pass without a valid datagram, when
// it is ended, or when another phone takes its player. Loop thread only, apart from sessions() and
// controls().
class RemoteGamepad {
 public:
  static constexpr uint16_t kPort = 4214;
  static constexpr std::size_t kPacketBytes = 32;
  static constexpr uint8_t kVersion = 1;
  static constexpr int64_t kTimeoutMs = 1000;
  static constexpr std::size_t kMaxNameChars = 32;
  using Token = std::array<uint8_t, 16>;

  struct Session {
    uint32_t id = 0;
    int player = 0;
    std::string name;
  };

  explicit RemoteGamepad(std::function<int64_t()> clock) : clock_(std::move(clock)) {}

  // A fresh random token and the port opened, replacing the session that drives `player`; the new
  // session's number, or 0 with nothing changed when either fails.
  uint32_t start(const std::string& name, int player, Token& token);
  // Starts a session with a given token, replacing the one on `player`; its number.
  uint32_t begin(const std::string& name, int player, const Token& token);
  // False when no session has that number.
  bool end(uint32_t id);
  void endAll();
  // One datagram; true when it belongs to a session and is newer than the last one it took.
  bool receive(const uint8_t* data, std::size_t size);
  // Takes what the port holds, then ends the sessions that have gone quiet.
  void poll();
  void tick();

  // The running sessions, by player.
  std::vector<Session> sessions() const;
  // The phone's controls while a session drives `player`.
  bool controls(int player, HidControls& out) const;
  std::string name(int player) const;

 private:
  struct Slot {
    uint32_t id = 0;
    Token token{};
    std::string name;
    bool haveSeq = false;
    uint32_t seq = 0;
    int64_t heardAt = 0;
    HidControls controls;
  };

  std::function<int64_t()> clock_;
  UdpSocket socket_;

  mutable std::mutex mutex_;
  std::array<Slot, kGamepadPlayers> slots_;
  uint32_t nextId_ = 1;
};

// Overlay each phone on its player; a player without one reads its Bluetooth gamepad.
class GamepadMux : public GamepadInput {
 public:
  GamepadMux(const RemoteGamepad& remote, const GamepadInput* bluetooth) : remote_(remote), bluetooth_(bluetooth) {}

  PlayerInput input(int player) const override;
  std::string name(int player) const override;

 private:
  const RemoteGamepad& remote_;
  const GamepadInput* bluetooth_;
};

}
