#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "core/api/StateJson.h"
#include "platform/linux/LinuxMemory.h"

namespace awtrix {
class CoreEngine;
class IBoard;

// A part of the platform that knows more about the device than the runtime reads for itself,
// such as the TC002 supervisor's network state or the web update's status.
class DeviceFactsSource {
 public:
  virtual void addFacts(DeviceFacts&) const {}
  virtual void writeMembers(api::JsonWriter&) const {}

 protected:
  ~DeviceFactsSource() = default;
};

// Builds the device state for /api/v1/device and MQTT on Linux. Owned by the entry point and
// used from the main loop only; sources must outlive it.
class LinuxDeviceFacts {
 public:
  // resetReason: why this runtime started (awtrix-tc002d's --start-reason), "unknown" without one.
  LinuxDeviceFacts(CoreEngine& engine, IBoard& board, std::string uid, std::string boardType,
                   std::string resetReason = "unknown");
  void addSource(const DeviceFactsSource& source) { sources_.push_back(&source); }
  // Samples memory and refreshes the host name about once a second. Without a
  // supervisor, observeNetwork also reports the local interfaces to runtime().wifi.
  void tick(int64_t nowMs, bool observeNetwork = false);
  std::string json(bool scriptingRunning);
  std::string ipAddress() const;
  // The name /api/v1/device reports, as of the last tick.
  const std::string& hostname() const { return hostname_; }

 private:
  static constexpr int64_t kNameRefreshMs = 1000;
  std::string nodename() const;

  CoreEngine& engine_;
  IBoard& board_;
  std::string uid_;
  std::string boardType_;
  std::string resetReason_;
  std::vector<const DeviceFactsSource*> sources_;
  LinuxMemoryGauge memory_;
  std::string hostname_;
  int64_t nextNameMs_ = 0;
};

}
