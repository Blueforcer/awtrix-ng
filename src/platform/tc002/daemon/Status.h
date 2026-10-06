#pragma once

#include <cstdint>
#include <string>

#include "core/api/JsonWriter.h"
#include "platform/tc002/daemon/Autostart.h"
#include "platform/tc002/daemon/HardwareLease.h"
#include "platform/tc002/daemon/McuService.h"
#include "platform/tc002/daemon/RuntimeChild.h"
#include "platform/tc002/daemon/Service.h"
#include "platform/tc002/daemon/update/UpdateService.h"

namespace awtrix {
namespace tc002d {

struct DaemonInfo {
  std::string version;
  bool keepAdbTcp = false;
  int64_t startedAtMs = 0;
};

void appendDeviceState(api::JsonWriter& json, const DeviceState& state);
// Reply of the "status" control command. Absent services are left out.
std::string daemonStatus(const DaemonInfo& info, const DeviceState& state, const HardwareLease* lease,
                         const McuService* mcu, const RuntimeChild* runtime, int64_t nowMs,
                         const UpdateService* update = nullptr, const AutostartService* autostart = nullptr);
std::string okReply();

// Writes DeviceState transitions to the log: USB supply and battery percent (not millivolt
// drift), Wi-Fi link/address changes (not RSSI; the SSID only when it changed) and clock
// synchronization. At most kNetworkBurst network lines are written per window.
class StateLog {
 public:
  static constexpr unsigned kNetworkBurst = 20;
  static constexpr int64_t kNetworkWindowMs = 600000;

  explicit StateLog(const DeviceState& state) : state_(state) {}
  void power();
  void network(int64_t nowMs);
  void time();
  unsigned lines() const { return lines_; }

 private:
  const DeviceState& state_;
  unsigned lines_ = 0;
  bool powerKnown_ = false, usb_ = false;
  int percent_ = -1;
  bool networkKnown_ = false;
  tc002::NetworkStatus network_;
  int64_t windowStart_ = 0;
  unsigned windowLines_ = 0, suppressed_ = 0;
  bool timeKnown_ = false, synchronized_ = false;
};

// Writes one warning line at start and then every intervalMs, for a setting that must not be
// forgotten in a log that rotates.
class LogReminder : public Service {
 public:
  LogReminder(const char* name, std::string message, int64_t intervalMs);
  const char* name() const override { return name_; }
  bool start(int64_t nowMs) override;
  int64_t nextDeadlineMs() const override { return next_; }
  void onTime(int64_t nowMs) override;
  void requestStop(int64_t nowMs) override;
  unsigned lines() const { return lines_; }

 private:
  void emit(int64_t nowMs);

  const char* name_;
  std::string message_;
  int64_t intervalMs_;
  int64_t next_ = -1;
  unsigned lines_ = 0;
};

}
}
