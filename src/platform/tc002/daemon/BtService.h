#pragma once

#include <cstdint>
#include <functional>
#include <string>
#include <vector>

#include "core/api/JsonWriter.h"
#include "platform/posix/Files.h"
#include "platform/tc002/contract/SupervisorProtocol.h"
#include "platform/tc002/daemon/Service.h"

// Attaches the AIC8800DC Bluetooth UART to the kernel HCI driver while the runtime wants it;
// waits up to driverWaitMs for wlan0.
namespace awtrix {
namespace tc002d {

struct BtOptions {
  std::string path = "/dev/ttyS3";
  std::string macPath = "/sys/class/net/wlan0/address";
  int64_t replyTimeoutMs = 1000;
  unsigned attempts = 2;
  // Shorter than the runtime's own controller wait.
  int64_t driverWaitMs = 8000;
  int64_t driverPollMs = 250;
};

// The device and kernel calls, so a test can stand in for the controller and the line discipline.
class BtSystem {
 public:
  virtual ~BtSystem() = default;
  virtual int open(const std::string& path) = 0;
  virtual bool configure(int fd) = 0;
  virtual bool attach(int fd) = 0;
  virtual void detach(int fd) = 0;
  virtual bool readText(const std::string& path, std::string& out) = 0;
};

BtSystem& linuxBtSystem();

class BtService : public Service {
 public:
  using Status = std::function<void(const tc002::BluetoothStatus&)>;
  enum class State { Off, Starting, On };

  BtService(BtOptions options, BtSystem& system, Status status);

  const char* name() const override { return "bluetooth"; }
  bool start(int64_t nowMs) override;
  void pollInterest(std::vector<PollInterest>& out) const override;
  void onReady(int fd, short revents, int64_t nowMs) override;
  int64_t nextDeadlineMs() const override;
  void onTime(int64_t nowMs) override;
  void requestStop(int64_t nowMs) override;
  bool stopped() const override { return !line_.valid(); }

  // Takes effect on the next loop turn; repeated requests for the current state are answered again.
  void request(bool on);
  State state() const { return state_; }
  void appendStatus(api::JsonWriter& json) const;

  // The vendor setup, each entry opcode and parameters; exposed for the contract test.
  struct Command {
    uint16_t opcode = 0;
    std::vector<uint8_t> params;
  };
  static std::vector<Command> setup(const uint8_t address[6]);
  static bool parseMac(const std::string& text, uint8_t address[6]);

 private:
  void begin(int64_t nowMs);
  void sendCurrent(int64_t nowMs);
  void fail(const std::string& why, int64_t nowMs);
  void release(const char* why);
  void report(bool on, const std::string& error);

  BtOptions options_;
  BtSystem& system_;
  Status status_;
  posix::UniqueFd line_;
  State state_ = State::Off;
  bool wanted_ = false;
  bool pending_ = false;
  std::vector<Command> commands_;
  std::size_t step_ = 0;
  unsigned attempt_ = 0;
  int64_t deadline_ = -1;
  int64_t driverDeadline_ = -1;
  std::vector<uint8_t> input_;
  std::string lastError_;
  unsigned attaches_ = 0;
};

}
}
