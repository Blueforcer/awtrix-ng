#pragma once

#include <sys/types.h>

#include <cstdint>
#include <memory>
#include <string>

#include "platform/tc002/contract/tc002_layout.h"

#include "core/api/JsonWriter.h"
#include "platform/tc002/daemon/Service.h"

// Board preconditions and exclusive hardware for the runtime child: board model, USB OTG role
// (device mode, so ADB over the cable works without Wi-Fi), the GPIO35 panel latch (exported,
// output, idle high) with an SPI settings snapshot restored on shutdown, and EVIOCGRAB on the
// two TC002 input devices, which the runtime receives as descriptors 100 and 101.
namespace awtrix {
namespace tc002d {

struct SpiSettings {
  uint8_t mode = 0, bits = 0, lsbFirst = 0;
  uint32_t speedHz = 0;
};

struct LatchState {
  bool exported = false;
  std::string direction, value;
  bool idleHigh() const { return exported && direction == "out" && value == "1"; }
};

class PanelBackend {
 public:
  virtual ~PanelBackend() = default;
  virtual bool readLatch(LatchState& out) = 0;
  virtual bool exportLatch() = 0;
  virtual bool driveLatchHigh() = 0;
  // True when no other process holds the SPI device open.
  virtual bool panelUnowned() = 0;
  virtual bool readSpi(SpiSettings& out) = 0;
  virtual bool writeSpi(const SpiSettings& settings) = 0;
};

class InputBackend {
 public:
  virtual ~InputBackend() = default;
  // Opens exactly one "soc:gpio_keys_1" and one "knob_key" device (O_RDONLY|O_NONBLOCK).
  virtual bool discover(int& keys, int& knob) = 0;
  virtual bool grab(int fd, bool enable) = 0;
  virtual bool keysUp(int fd) = 0;
  virtual bool drain(int fd, uint64_t& discarded) = 0;
};

// Device paths are prefixed with root ("" on the device, a fake tree in tests).
std::unique_ptr<PanelBackend> nativePanelBackend(const std::string& root);
std::unique_ptr<InputBackend> nativeInputBackend(const std::string& root);

// What the runtime child needs from the lease. prepareChild runs only while no child exists:
// draining or EVIOCGKEY on the shared descriptions would steal the child's queued events.
class ChildHardware {
 public:
  virtual ~ChildHardware() = default;
  virtual bool childReady() const = 0;
  virtual bool prepareChild(int64_t nowMs) = 0;
  virtual int keysFd() const = 0;
  virtual int knobFd() const = 0;
};

struct LeaseOptions {
  std::string root;
  int64_t quietMs = 100;
  int64_t grabRetryMs = 1000;
  int64_t usbCheckFastMs = 5000;
  int64_t usbCheckSlowMs = 30000;
  int64_t usbFastPhaseMs = 120000;
  // While the kernel's USB scan runs otg_role reads "unkown" and a write only costs a bus reset.
  int64_t usbUnknownPollMs = 500;
  int64_t usbUnknownPhaseMs = 20000;
  int64_t usbRecheckMs = 1000;
  int64_t usbRetryMs = 10000;
  int64_t releaseTimeoutMs = 2000;
};

class HardwareLease : public Service, public ChildHardware {
 public:
  static constexpr const char* kBoardModel = TC002_BOARD_MODEL;

  HardwareLease(LeaseOptions options, std::unique_ptr<PanelBackend> panel, std::unique_ptr<InputBackend> input);
  ~HardwareLease() override;

  const char* name() const override { return "lease"; }
  bool start(int64_t nowMs) override;
  int64_t nextDeadlineMs() const override;
  void onTime(int64_t nowMs) override;
  bool onChildExit(pid_t pid, int status, int64_t nowMs) override;
  void requestStop(int64_t nowMs) override;
  bool stopped() const override { return phase_ == Phase::Released; }

  bool childReady() const override { return phase_ == Phase::Held; }
  bool prepareChild(int64_t nowMs) override;
  int keysFd() const override { return keys_; }
  int knobFd() const override { return knob_; }

  void appendStatus(api::JsonWriter& json) const;
  const std::string& otgRole() const { return otgRole_; }
  unsigned otgWrites() const { return otgWrites_; }
  pid_t otgWriter() const { return otgWriter_; }

 private:
  enum class Phase { Idle, Quiet, Held, Releasing, Released };

  bool boardMatches();
  void checkUsbRole(int64_t nowMs);
  void startUsbWrite(int64_t nowMs);
  bool ensureLatch(const char* when);
  bool drainBoth();
  void attemptGrab(int64_t nowMs);
  void releaseInputs();
  void restorePanel();

  LeaseOptions options_;
  std::unique_ptr<PanelBackend> panel_;
  std::unique_ptr<InputBackend> input_;
  Phase phase_ = Phase::Idle;
  int keys_ = -1, knob_ = -1;
  bool keysGrabbed_ = false, knobGrabbed_ = false;
  uint64_t discarded_ = 0;
  int64_t startedAt_ = 0;
  int64_t quietUntil_ = -1;
  int64_t releaseDeadline_ = -1;
  int64_t releaseTimeoutAt_ = -1;
  unsigned waitNotices_ = 0;

  SpiSettings savedSpi_;
  bool spiSaved_ = false;
  unsigned latchRepairs_ = 0;

  std::string otgRole_;
  unsigned otgWrites_ = 0;
  unsigned otgFailures_ = 0;
  int64_t nextUsbCheck_ = -1;
  pid_t otgWriter_ = -1;
  int64_t otgWriteAt_ = -1;
};

}
}
