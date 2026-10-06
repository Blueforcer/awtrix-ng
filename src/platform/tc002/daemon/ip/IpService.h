#pragma once

#include <memory>
#include <string>

#include "platform/tc002/daemon/Service.h"

namespace awtrix {
namespace tc002d {

struct IpOptions {
  std::string runDir, udhcpc, dhcpCallback;
  // Where the fixed address is kept between starts, so that a boot never asks DHCP first.
  std::string staticFile;
  std::string ntpServer = "pool.ntp.org";
  std::string hostname;
  int httpPort = 80;
};

// IPv4 on wlan0 above the Wi-Fi link: a fixed address from the runtime or a BusyBox udhcpc child
// whose callback records are applied through rtnetlink, our resolv.conf bind-mounted over
// /etc/resolv.conf, SNTP, the kernel hostname and an mDNS responder for <hostname>.local.
class IpService : public Service, public TimeControl, public HostnameControl, public AddressControl {
 public:
  IpService(DeviceState& state, IpOptions options);
  ~IpService() override;

  const char* name() const override { return "ip"; }
  bool start(int64_t nowMs) override;
  void pollInterest(std::vector<PollInterest>& out) const override;
  void onReady(int fd, short revents, int64_t nowMs) override;
  int64_t nextDeadlineMs() const override;
  void onTime(int64_t nowMs) override;
  bool onChildExit(pid_t pid, int status, int64_t nowMs) override;
  void requestStop(int64_t nowMs) override;
  bool stopped() const override;

  void setServer(const std::string& server) override;
  void setHostname(const std::string& hostname) override;
  void setStaticAddress(const tc002::StaticAddress& address) override;

 private:
  struct Impl;
  std::unique_ptr<Impl> impl_;
};

}
}
