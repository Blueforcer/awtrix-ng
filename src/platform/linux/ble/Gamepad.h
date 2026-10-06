#pragma once

#include <array>
#include <cstdint>
#include <functional>
#include <set>
#include <string>

#include "platform/linux/ble/BleHub.h"
#include "platform/linux/ble/HidGamepad.h"
#include "platform/linux/ble/GamepadState.h"

namespace awtrix::ble {

// One HID session. It owns its BLE resources and reports what changes through the callbacks; the
// owner keeps persistence and player assignment. All calls run on the Bluetooth worker.
class Gamepad {
 public:
  struct Known {
    Address address;
    std::string name;
  };
  static constexpr int64_t kPairingMs = 60000;
  // Each failure in a row doubles the wait before the next try, up to kRetryMaxMs.
  static constexpr int64_t kRetryMs = 2000;
  static constexpr int64_t kRetryMaxMs = 60000;

  Gamepad(BleHub& hub, std::string owner, std::function<void(const std::string&)> log);
  void remember(const Known& known);
  const Known& known() const { return known_; }
  bool paired() const { return haveKnown_; }
  bool pairing() const { return pairing_; }
  const std::string& owner() const { return owner_; }
  GamepadStatus status() const { return status_; }
  // The device the status is about: the one being connected, else the remembered one, else none.
  const Known* device() const;

  // Set before start().
  std::function<bool(const Address&)> acceptPeer;
  // The remembered device was paired, replaced or forgotten.
  std::function<void()> onKnownChanged;
  // status() or device() may have changed.
  std::function<void()> onStatus;
  // A report from the Ready device.
  std::function<void(const HidControls&)> onControls;

  void start(int64_t nowMs);
  // An event the hub sent to owner(). Never from inside the hub: it is mid-operation when it sends.
  void handle(const BleEvent& e, int64_t nowMs);
  void tick(int64_t nowMs);

  void pair(int64_t nowMs);
  void forget(int64_t nowMs);

 private:
  void stopAll();
  void searchNew();
  void watch();
  void link(const Address& address, const std::string& name);
  void lost(const std::string& why);
  void subscribe();
  void ready();
  void publish(GamepadStatus status);
  std::string call(const char* op, const std::string& args, uint32_t id);
  uint32_t next() { return ++lastId_; }

  BleHub& hub_;
  std::string owner_;
  std::function<void(const std::string&)> log_;
  int64_t now_ = 0;

  bool haveKnown_ = false;
  Known known_;
  bool pairing_ = false;
  int64_t pairingUntil_ = -1;
  // Devices found while pairing that turned out not to be gamepads.
  std::set<std::array<uint8_t, 6>> rejected_;
  int64_t retryAt_ = -1;
  int failures_ = 0;
  uint32_t lastId_ = 0;
  uint32_t scanId_ = 0, connId_ = 0, readId_ = 0, subId_ = 0;
  Known linking_;
  HidLayout layout_;
  GamepadStatus status_ = GamepadStatus::Unpaired;
  // Reused for every report.
  std::string hex_;
  Bytes report_;
};

}
