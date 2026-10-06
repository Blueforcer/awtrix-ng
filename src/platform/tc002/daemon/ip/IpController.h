#pragma once

#include <sys/types.h>

#include <cstdint>
#include <string>
#include <vector>

#include "platform/tc002/daemon/Service.h"
#include "platform/tc002/daemon/ip/CaptiveDns.h"
#include "platform/tc002/daemon/ip/ChildProcess.h"
#include "platform/tc002/daemon/ip/ClientOutput.h"
#include "platform/tc002/daemon/ip/DhcpLease.h"
#include "platform/tc002/daemon/ip/IpService.h"
#include "platform/tc002/daemon/ip/LeaseApplier.h"
#include "platform/tc002/daemon/ip/Mdns.h"
#include "platform/tc002/daemon/ip/Netlink.h"
#include "platform/tc002/daemon/ip/ResolvConf.h"
#include "platform/tc002/daemon/ip/Sntp.h"

namespace awtrix {
namespace tc002d {
namespace ip {

constexpr const char* kInterface = "wlan0";
constexpr int kDefaultHttpPort = 80;

// Everything the controller touches outside its own memory, so tests can replace it.
class IpPlatform {
 public:
  virtual ~IpPlatform() = default;
  virtual IpKernel& kernel() = 0;
  virtual SystemClock& clock() = 0;
  virtual ResolverFile& resolver() = 0;
  virtual pid_t spawn(const SpawnRequest& request, std::string& error) = 0;
  virtual void signal(pid_t pid, int number, bool wholeGroup) = 0;
  virtual bool setKernelHostname(const std::string& hostname) = 0;
  virtual std::string interfaceMac(const std::string& interfaceName) = 0;
  virtual bool executable(const std::string& path) = 0;
  virtual bool prepareDirectory(const std::string& path) = 0;
  virtual bool readFile(const std::string& path, std::string& out) = 0;
  // Atomic; removeFile succeeds for a missing file.
  virtual bool replaceFile(const std::string& path, const std::string& text) = 0;
  virtual bool removeFile(const std::string& path) = 0;
  virtual bool prepareAccessPoint(const std::string& directory, const std::string& config, std::string& error) = 0;
  virtual void removeAccessPoint(const std::string& directory) = 0;
  virtual bool startCaptiveDns(CaptiveDns& responder, std::string& error) {
    return responder.start(kInterface, error);
  }
  virtual int64_t monotonicMs() = 0;
  virtual bool multicast() = 0;
  virtual bool startMdns(MdnsResponder& responder, const mdns::Identity& identity, int ifindex,
                         uint8_t prefix, int64_t nowMs, std::string& error) {
    return responder.start(identity, ifindex, prefix, nowMs, error);
  }
  virtual void log(const char* component, const std::string& line) = 0;
};

struct IpTiming {
  int64_t linkLossGraceMs = 20 * 1000;
  int64_t leaseLossGraceMs = 20 * 1000;
  int64_t clientStopGraceMs = 2000;
  int64_t firstRestartMs = 1000;
  int64_t maxRestartMs = 60 * 1000;
  int64_t stableClientMs = 60 * 1000;
  int64_t resolverRetryMs = 10 * 1000;
  int64_t mdnsRetryMs = 10 * 1000;
};

// Until our resolv.conf is bound over the stock one (which names 114.114.114.114), a lease is
// applied without its default route and without DNS, so nothing resolves through the stock file.
class IpController {
 public:
  IpController(DeviceState& state, IpOptions options, IpPlatform& platform, IpTiming timing = IpTiming(),
               SntpTiming sntpTiming = SntpTiming());
  ~IpController();
  IpController(const IpController&) = delete;
  IpController& operator=(const IpController&) = delete;

  bool start(int64_t nowMs);
  void pollInterest(std::vector<PollInterest>& out) const;
  void onReady(int fd, short revents, int64_t nowMs);
  int64_t nextDeadlineMs() const;
  void onTime(int64_t nowMs);
  bool onChildExit(pid_t pid, int status, int64_t nowMs);
  void requestStop(int64_t nowMs);
  bool stopped() const;
  void setServer(const std::string& server);
  void setHostname(const std::string& hostname);
  // A valid enabled address replaces udhcpc and is kept in IpOptions::staticFile for the next
  // start; anything else means DHCP.
  void setStaticAddress(const tc002::StaticAddress& address);

  pid_t dhcpClient() const { return dhcpPid_; }
  const std::string& hostname() const { return hostname_; }
  bool leaseApplied() const { return applier_.applied(); }
  // Feeds one callback line as if it had arrived on the event pipe.
  void handleRecord(const std::string& line, int64_t nowMs);

 private:
  void log(const std::string& line) { platform_.log("ip", line); }
  void installResolver(int64_t nowMs);
  void processNetwork(int64_t nowMs);
  bool resolveHostname();
  void hostnameChanged(int64_t nowMs);
  void linkUp(int64_t nowMs);
  void linkDown(int64_t nowMs);
  void startClient(int64_t nowMs, bool flushFirst);
  void loadStatic();
  void applyStatic(int64_t nowMs, bool flushFirst);
  void addressingChanged(int64_t nowMs);
  void startAccessPoint(int64_t nowMs);
  void stopAccessPoint(int64_t nowMs);
  void startWantedClient(int64_t nowMs, bool flushFirst);
  void stopClient(int64_t nowMs, bool restartAfter);
  void scheduleRestart(int64_t nowMs);
  void readEvents(int64_t nowMs);
  void readClientOutput(int64_t nowMs, bool exited);
  void handleEvent(const DhcpRecord& record, int64_t nowMs);
  void applyLease(DhcpLease lease, const char* source, int64_t capturedMs, const std::string& notes, int64_t nowMs);
  void removeLease(const char* reason, int64_t nowMs);
  void publish(int64_t nowMs);
  void updateServices(uint32_t address, uint8_t prefix, int ifindex, int64_t nowMs);
  void publishTime();
  mdns::Identity identityFor(uint32_t address) const;

  DeviceState& state_;
  IpOptions options_;
  IpPlatform& platform_;
  IpTiming timing_;
  LeaseApplier applier_;
  SntpClient sntp_;
  MdnsResponder mdns_;
  CaptiveDns captiveDns_;
  bool accessPoint_ = false, dhcpServer_ = false;
  bool started_ = false, stopping_ = false, networkPending_ = false, publishing_ = false, linkUp_ = false;
  bool resolverInstalled_ = false;
  std::string resolverError_;
  int64_t resolverRetryAtMs_ = -1;
  uint32_t withheldRouter_ = 0;
  int64_t nowMs_ = 0;
  std::string hostname_, mac_;
  DhcpLease static_;
  int mdnsIfindex_ = 0;
  uint8_t mdnsPrefix_ = 0;
  mdns::Identity mdnsIdentity_;
  int64_t mdnsRetryAtMs_ = -1;

  pid_t dhcpPid_ = -1;
  int dhcpIfindex_ = 0;
  int eventRead_ = -1, eventWrite_ = -1, clientOutput_ = -1;
  ClientOutput clientLog_;
  std::string eventBuffer_;
  bool restartAfterExit_ = false, flushOnRestart_ = false;
  int64_t dhcpStartedMs_ = -1, dhcpRestartAtMs_ = -1, dhcpKillAtMs_ = -1, restartBackoffMs_ = 0;
  int64_t removeAtMs_ = -1, leaseExpiresMs_ = -1;
};

}
}
}
