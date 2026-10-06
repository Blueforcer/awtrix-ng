#pragma once

#include <atomic>
#include <condition_variable>
#include <deque>
#include <functional>
#include <memory>
#include <mutex>
#include <string>
#include <thread>

#include "platform/linux/ble/BleBackend.h"
#include "platform/linux/ble/BleHub.h"
#include "platform/linux/ble/GamepadManager.h"
#include "platform/linux/ble/IphoneLink.h"
#include "platform/linux/ble/LinuxBleRadio.h"

namespace awtrix::ble {

// Bluetooth for the script host, the gamepad and the iPhone link: one worker thread owns the
// controller's sockets, the hub, the gamepad and the link. Script calls cross over and wait a
// moment for their synchronous answer; events come back through the sink, which must be safe to
// call from the worker. Requests to attach or release the controller are left for the main thread,
// which owns the supervisor channel.
class BleService : public BleBackend, public IphoneControl {
 public:
  struct Options {
    std::string bondsPath;
    std::string gamepadPath;
    std::string name;
    bool supervised = false;
    // The gamepad is kept connected only for scripts.
    bool gamepad = true;
  };

  BleService(Options options, std::function<void(BleEvent)> sink);
  ~BleService() override;

  void start();
  void stop();

  std::string call(const std::string& script, const std::string& op, const std::string& args,
                   uint32_t id) override;
  void forget(const std::string& script) override;

  // Any thread: what the gamepads are doing and what their controls read.
  const GamepadRegistry& gamepad() const { return *gamepad_; }
  // Any thread: look for a new gamepad for a minute in a free slot, or let go of the one in slot id.
  GamepadPairResult pairGamepad();
  void forgetGamepad(int id);

  IphoneStatus iphoneStatus() const override { return iphone_->status(); }
  std::deque<IphoneEvent> takeIphoneEvents() override { return iphone_->take(); }
  void configureIphone(const IphoneConfig& config) override;
  void forgetIphone() override;

  // Main thread: the supervisor's answer, and the next attach (1) or release (0) to send, or -1.
  void controllerStatus(bool on, const std::string& error);
  int takeControllerRequest();
  // Main thread: hands over what the worker logged, as the log is not safe to write from it.
  void drainLog(const std::function<void(const std::string&)>& write);

 private:
  struct Command {
    std::function<void()> run;
  };
  void post(std::function<void()> run);
  void loop();

  Options options_;
  std::function<void(BleEvent)> sink_;
  std::unique_ptr<LinuxBleRadio> radio_;
  std::unique_ptr<BleHub> hub_;
  std::unique_ptr<GamepadManager> gamepad_;
  std::unique_ptr<IphoneLink> iphone_;
  std::thread worker_;
  std::mutex mutex_;
  std::deque<Command> commands_;
  int wake_[2] = {-1, -1};
  std::atomic<bool> stopping_{false};
  std::atomic<int> controllerRequest_{-1};
  std::mutex logMutex_;
  std::deque<std::string> logLines_;
};

}
