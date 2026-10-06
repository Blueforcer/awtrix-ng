#pragma once

#include <array>
#include <deque>
#include <functional>
#include <memory>
#include <mutex>
#include <string>

#include "platform/linux/ble/Gamepad.h"

namespace awtrix::ble {

// The two stored gamepads: one session per device slot, the file that remembers them, and the
// players. When exactly one device is Ready it is Player 1; a second one becomes Player 2, so two
// keep the order of their first valid reports. The worker owns the sessions; other threads read
// the copy published under the lock, which a report updates in place.
class GamepadManager : public GamepadRegistry {
 public:
  GamepadManager(BleHub& hub, std::string path, std::function<void(const std::string&)> log);
  bool owns(const std::string& owner) const;
  // Any session's events, kept in one queue so reports from both devices stay in arrival order.
  void onEvent(BleEvent event);
  void start(int64_t nowMs);
  void tick(int64_t nowMs);
  GamepadPairResult pair(int64_t nowMs);
  // id is the device slot, 1..kGamepadPlayers.
  void forget(int id, int64_t nowMs);

  PlayerInput input(int player) const override;
  std::string name(int player) const override;
  GamepadDevices devices() const override;

 private:
  void statusChanged(int slot);
  void load();
  bool loadDevices(const std::string& json);
  void save();

  std::string path_;
  std::function<void(const std::string&)> log_;
  std::array<std::unique_ptr<Gamepad>, kGamepadPlayers> sessions_;
  std::deque<BleEvent> events_;
  // The Ready slots in the order they became Ready: the slot of Player 1 first, -1 for none.
  std::array<int, kGamepadPlayers> order_{{-1, -1}};

  mutable std::mutex mutex_;
  GamepadDevices devices_;
  // Per slot, the last report while Ready.
  std::array<HidControls, kGamepadPlayers> controls_;
  // Per player, the slot it reads: its Ready device, or one that is not, to show its progress.
  std::array<int, kGamepadPlayers> shown_{{0, 1}};
};

}
