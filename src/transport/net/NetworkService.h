#pragma once

#include <DNSServer.h>

#include <cstdint>
#include <functional>
#include <string>

#include "core/net/LinkStatus.h"
#include "persistence/DeviceConfig.h"

namespace awtrix {

class NetworkService {
 public:
  // status is written on every association change and outlives this service; the caller owns it.
  // Passing it before begin() means the boot join is recorded too.
  void setStatus(net::LinkStatus* status) { status_ = status; }
  void begin(const DeviceConfig& cfg, bool forceAp = false,
             const std::function<void()>& onWait = nullptr);
  void tick();

  void setOnJoinedFromAp(std::function<void()> fn) { onJoinedFromAp_ = std::move(fn); }
  bool isConnected() const;
  bool apMode() const { return apMode_; }
  std::string ip() const;
  const std::string& hostname() const { return hostname_; }

 private:
  void retryJoinFromAp();
  void startAp(const char* why);
  void roamIfWeak(unsigned long nowMs);
  void publishStatus();

  net::LinkStatus* status_ = nullptr;
  bool apMode_ = false;
  std::string hostname_;
  unsigned long lastCheckMs_ = 0;
  unsigned long lastApRetryMs_ = 0;
  unsigned long lastRoamMs_ = 0;
  int weakChecks_ = 0;
  bool apPaused_ = false;  // RP2040: provisioning AP down for a station-only join window
  bool restartPending_ = false;  // RP2040: the reboot after a join from the AP is deferred
  const DeviceConfig* cfg_ = nullptr;
  std::function<void()> onJoinedFromAp_;
  DNSServer dns_;
};

}
