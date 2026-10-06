#include "platform/tc002/daemon/ip/IpController.h"
#include "platform/tc002/contract/tc002_layout.h"

#include <algorithm>
#include <cerrno>
#include <csignal>
#include <cstring>
#include <fcntl.h>
#include <poll.h>
#include <sys/wait.h>
#include <unistd.h>

#include "platform/tc002/daemon/ip/DhcpRecordFormat.h"
#include "platform/tc002/daemon/ip/Hostname.h"
#include "platform/tc002/daemon/ip/Ipv4.h"
#include "platform/posix/Files.h"

namespace awtrix {
namespace tc002d {
namespace ip {
namespace {

constexpr const char* kDefaultNtpServer = "pool.ntp.org";
constexpr int64_t kMaxCallbackAgeMs = 60 * 1000;

void closeFd(int& fd) {
  if (fd >= 0) ::close(fd);
  fd = -1;
}

std::string describeExit(int status) {
  if (WIFEXITED(status)) return "status " + std::to_string(WEXITSTATUS(status));
  if (WIFSIGNALED(status)) return std::string("signal ") + std::to_string(WTERMSIG(status));
  return "status " + std::to_string(status);
}

bool validServerName(const std::string& name) {
  if (name.empty() || name.size() > 253) return false;
  for (char c : name) {
    const bool letter = (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z');
    if (!letter && !(c >= '0' && c <= '9') && c != '-' && c != '.') return false;
  }
  return true;
}

bool sameIdentity(const mdns::Identity& a, const mdns::Identity& b) {
  if (a.hostname != b.hostname || a.address != b.address || a.services.size() != b.services.size()) return false;
  for (std::size_t i = 0; i < a.services.size(); ++i) {
    if (a.services[i].type != b.services[i].type || a.services[i].port != b.services[i].port ||
        a.services[i].txt != b.services[i].txt)
      return false;
  }
  return true;
}

std::string seconds(int64_t ms) {
  return ms % 1000 ? std::to_string(ms) + " ms" : std::to_string(ms / 1000) + " s";
}

std::string serverList(const std::vector<uint32_t>& servers) {
  std::string out;
  for (uint32_t server : servers) out += (out.empty() ? "" : ",") + formatIpv4(server);
  return out.empty() ? "-" : out;
}

}

IpController::IpController(DeviceState& state, IpOptions options, IpPlatform& platform, IpTiming timing,
                           SntpTiming sntpTiming)
    : state_(state),
      options_(std::move(options)),
      platform_(platform),
      timing_(timing),
      applier_(platform.kernel(), kInterface),
      sntp_(platform.clock(), sntpTiming),
      clientLog_([this](const std::string& line) { platform_.log(dhcpServer_ ? "udhcpd" : "udhcpc", line); }) {
  sntp_.log = [this](const std::string& line) { platform_.log("sntp", line); };
  sntp_.synchronizedChanged = [this] { publishTime(); };
  loadStatic();
}

IpController::~IpController() {
  if (dhcpPid_ > 0) platform_.signal(dhcpPid_, SIGKILL, true);
  closeFd(eventRead_);
  closeFd(eventWrite_);
  closeFd(clientOutput_);
}

bool IpController::start(int64_t nowMs) {
  nowMs_ = nowMs;
  if (started_) return true;
  if (options_.runDir.empty() || options_.udhcpc.empty() || options_.dhcpCallback.empty()) {
    log("needs runDir, udhcpc and dhcpCallback");
    return false;
  }
  if (!options_.hostname.empty() && !validHostname(options_.hostname)) {
    log("ignoring invalid hostname \"" + options_.hostname + "\"");
    options_.hostname.clear();
  }
  if (options_.ntpServer.empty() || !validServerName(options_.ntpServer)) options_.ntpServer = kDefaultNtpServer;
  if (options_.httpPort <= 0 || options_.httpPort > 65535) options_.httpPort = kDefaultHttpPort;
  int ends[2];
  if (::pipe2(ends, O_CLOEXEC | O_NONBLOCK) != 0) {
    log(std::string("event pipe: ") + std::strerror(errno));
    return false;
  }
  eventRead_ = ends[0];
  eventWrite_ = ends[1];
  if (!platform_.prepareDirectory(options_.runDir)) log("cannot create " + options_.runDir);
  installResolver(nowMs);
  started_ = true;
  sntp_.setServer(options_.ntpServer, nowMs);
  if (resolveHostname()) log("hostname " + hostname_);
  state_.onNetwork([this] {
    if (!publishing_ && !stopping_) networkPending_ = true;
  });
  networkPending_ = true;
  publish(nowMs);
  return true;
}

void IpController::installResolver(int64_t nowMs) {
  std::string error;
  resolverInstalled_ = platform_.resolver().install(error);
  if (!resolverInstalled_) {
    resolverRetryAtMs_ = nowMs + timing_.resolverRetryMs;
    if (error.empty()) error = "not bound";
    if (error != resolverError_) log("resolv.conf: " + error + "; no DNS and no default route until it is bound");
    resolverError_ = error;
    return;
  }
  resolverRetryAtMs_ = -1;
  if (!resolverError_.empty()) log("resolv.conf bound");
  resolverError_.clear();
  if (!applier_.applied() || accessPoint_) return;
  DhcpLease lease = applier_.lease();
  lease.router = withheldRouter_;
  withheldRouter_ = 0;
  if (!applier_.apply(lease, error)) log("default route: " + error);
  if (!platform_.resolver().write(resolvContent(lease.dns, lease.router), error)) log("resolv.conf: " + error);
  publish(nowMs);
}

bool IpController::resolveHostname() {
  const std::string& published = state_.network().mac;
  uint8_t bytes[6];
  if (parseMac(published, bytes)) mac_ = published;
  else if (mac_.empty()) mac_ = platform_.interfaceMac(kInterface);
  const std::string wanted = !options_.hostname.empty() ? options_.hostname : hostnameForMac(mac_);
  if (wanted.empty() || wanted == hostname_) return false;
  hostname_ = wanted;
  if (!platform_.setKernelHostname(hostname_)) log(std::string("sethostname: ") + std::strerror(errno));
  return true;
}

void IpController::hostnameChanged(int64_t nowMs) {
  log("hostname " + hostname_);
  if (dhcpPid_ > 0 && !dhcpServer_ && !accessPoint_) {
    log("restarting udhcpc to announce the new hostname");
    stopClient(nowMs, true);
  }
  publish(nowMs);
}

void IpController::processNetwork(int64_t nowMs) {
  networkPending_ = false;
  if (resolveHostname()) hostnameChanged(nowMs);
  const bool up = state_.network().link == tc002::WifiLink::Connected;
  const bool ap = state_.network().link == tc002::WifiLink::AccessPoint;
  if (ap != accessPoint_) {
    accessPoint_ = ap;
    linkUp_ = up;
    removeAtMs_ = leaseExpiresMs_ = dhcpRestartAtMs_ = -1;
    restartBackoffMs_ = 0;
    withheldRouter_ = 0;
    stopAccessPoint(nowMs);
    removeLease("network mode changed", nowMs);
    eventBuffer_.clear();
    readEvents(nowMs);
    flushOnRestart_ = true;
    if (dhcpPid_ > 0) stopClient(nowMs, true);
    else startWantedClient(nowMs, true);
  } else if (up != linkUp_) {
    linkUp_ = up;
    if (up) linkUp(nowMs);
    else linkDown(nowMs);
  }
  publish(nowMs);
}

void IpController::linkUp(int64_t nowMs) {
  removeAtMs_ = -1;
  if (dhcpPid_ > 0) {
    const int ifindex = platform_.kernel().interfaceIndex(kInterface);
    if (dhcpKillAtMs_ >= 0 || dhcpServer_) {
      restartAfterExit_ = true;
      flushOnRestart_ = !applier_.applied();
      stopClient(nowMs, true);
      return;
    }
    if (ifindex && ifindex == dhcpIfindex_) {
      log("link back, renewing the lease");
      platform_.signal(dhcpPid_, SIGUSR1, false);
      return;
    }
    log("wlan0 was recreated, restarting udhcpc");
    flushOnRestart_ = true;
    stopClient(nowMs, true);
    return;
  }
  restartBackoffMs_ = 0;
  startClient(nowMs, !applier_.applied());
}

void IpController::linkDown(int64_t nowMs) {
  dhcpRestartAtMs_ = -1;
  if ((applier_.applied() || dhcpPid_ > 0) && removeAtMs_ < 0) removeAtMs_ = nowMs + timing_.linkLossGraceMs;
}

void IpController::startClient(int64_t nowMs, bool flushFirst) {
  if (dhcpPid_ > 0 || stopping_ || accessPoint_) return;
  dhcpRestartAtMs_ = -1;
  if (static_.address) {
    applyStatic(nowMs, flushFirst);
    return;
  }
  if (!platform_.executable(options_.udhcpc) || !platform_.executable(options_.dhcpCallback)) {
    log("udhcpc not started: " + options_.udhcpc + " or " + options_.dhcpCallback + " is not executable");
    scheduleRestart(nowMs);
    return;
  }
  const int ifindex = platform_.kernel().interfaceIndex(kInterface);
  if (!ifindex) {
    log("udhcpc not started: wlan0 is missing");
    scheduleRestart(nowMs);
    return;
  }
  if (flushFirst) {
    const bool hadLease = applier_.applied();
    std::string error;
    if (!applier_.flush(error)) log("clearing stale IPv4 state on wlan0: " + error);
    if (hadLease) publish(nowMs);
  }
  SpawnRequest request;
  request.path = options_.udhcpc;
  request.argv = {"udhcpc", "-f", "-i", kInterface, "-s", options_.dhcpCallback,
                  "-t", "4", "-T", "3", "-A", "10", "-V", ""};
  if (!hostname_.empty()) {
    request.argv.push_back("-x");
    request.argv.push_back("hostname:" + hostname_);
  }
  if (applier_.applied()) {
    request.argv.push_back("-r");
    request.argv.push_back(formatIpv4(applier_.lease().address));
  }
  request.environment = {"PATH=/usr/sbin:/usr/bin:/sbin:/bin"};
  request.passFd = eventWrite_;
  request.passAs = kDhcpEventFd;
  request.discardStdout = true;
  int output[2] = {-1, -1};
  if (::pipe2(output, O_CLOEXEC | O_NONBLOCK) == 0) request.stderrFd = output[1];
  std::string error;
  const pid_t pid = platform_.spawn(request, error);
  closeFd(output[1]);
  if (pid <= 0) {
    closeFd(output[0]);
    log("udhcpc not started: " + error);
    scheduleRestart(nowMs);
    return;
  }
  readClientOutput(nowMs, true);
  clientOutput_ = output[0];
  dhcpPid_ = pid;
  dhcpServer_ = false;
  dhcpIfindex_ = ifindex;
  dhcpStartedMs_ = nowMs;
  dhcpKillAtMs_ = -1;
  log("udhcpc started, pid " + std::to_string(pid));
}

void IpController::applyStatic(int64_t nowMs, bool flushFirst) {
  if (!flushFirst && applier_.applied() && applier_.lease().address == static_.address &&
      applier_.lease().prefix == static_.prefix)
    return;
  std::string error;
  if (flushFirst && !applier_.flush(error)) log("clearing stale IPv4 state on wlan0: " + error);
  applyLease(static_, "static", -1, std::string(), nowMs);
  if (applier_.applied()) return;
  publish(nowMs);
  scheduleRestart(nowMs);
}

void IpController::addressingChanged(int64_t nowMs) {
  if (accessPoint_) return;
  removeAtMs_ = leaseExpiresMs_ = dhcpRestartAtMs_ = -1;
  restartBackoffMs_ = 0;
  removeLease("addressing changed", nowMs);
  eventBuffer_.clear();
  readEvents(nowMs);
  flushOnRestart_ = true;
  if (dhcpPid_ > 0) stopClient(nowMs, true);
  else startWantedClient(nowMs, true);
  publish(nowMs);
}

void IpController::startWantedClient(int64_t nowMs, bool flushFirst) {
  if (accessPoint_) startAccessPoint(nowMs);
  else if (linkUp_) startClient(nowMs, flushFirst);
}

void IpController::startAccessPoint(int64_t nowMs) {
  if (dhcpPid_ > 0 || stopping_ || !accessPoint_) return;
  dhcpRestartAtMs_ = -1;
  std::string error;
  const auto failed = [&] {
    log("access point not ready: " + error);
    stopAccessPoint(nowMs);
    platform_.removeAccessPoint(options_.runDir);
    scheduleRestart(nowMs);
  };
  if (!platform_.executable(options_.udhcpc)) {
    error = "BusyBox is not executable";
    failed();
    return;
  }
  if (!applier_.flush(error)) {
    failed();
    return;
  }
  const std::string privatePath = options_.runDir + "/access-point/";
  const std::string config = "interface wlan0\nstart 192.168.4.2\nend 192.168.4.9\nmax_leases 8\n"
                             "option subnet 255.255.255.0\noption router " TC002_AP_ADDRESS "\n"
                             "option dns " TC002_AP_ADDRESS "\noption lease 600\nmin_lease 60\n"
                             "auto_time 60\nlease_file " + privatePath + "leases\n";
  if (!platform_.prepareAccessPoint(options_.runDir, config, error)) {
    failed();
    return;
  }
  DhcpLease address;
  address.address = kAccessPointAddress;
  address.prefix = kAccessPointPrefix;
  address.broadcast = kAccessPointAddress | 0xff;
  address.leaseSeconds = kInfiniteLease;
  if (!applier_.apply(address, error) || !platform_.startCaptiveDns(captiveDns_, error)) {
    failed();
    return;
  }
  SpawnRequest request;
  request.path = options_.udhcpc;
  request.argv = {"udhcpd", "-f", privatePath + "udhcpd.conf"};
  request.environment = {"PATH=/usr/sbin:/usr/bin:/sbin:/bin"};
  request.discardStdout = true;
  int output[2] = {-1, -1};
  if (::pipe2(output, O_CLOEXEC | O_NONBLOCK) == 0) request.stderrFd = output[1];
  const pid_t pid = platform_.spawn(request, error);
  closeFd(output[1]);
  if (pid <= 0) {
    closeFd(output[0]);
    failed();
    return;
  }
  readClientOutput(nowMs, true);
  clientOutput_ = output[0];
  dhcpPid_ = pid;
  dhcpServer_ = true;
  dhcpIfindex_ = applier_.ifindex();
  dhcpStartedMs_ = nowMs;
  dhcpKillAtMs_ = -1;
  removeAtMs_ = leaseExpiresMs_ = -1;
  log("access point ready at " TC002_AP_ADDRESS ", udhcpd pid " + std::to_string(pid));
  publish(nowMs);
}

void IpController::stopAccessPoint(int64_t nowMs) {
  captiveDns_.stop();
  if (applier_.applied() && applier_.lease().address == kAccessPointAddress)
    removeLease("access point stopped", nowMs);
  if (dhcpPid_ <= 0) platform_.removeAccessPoint(options_.runDir);
}

void IpController::stopClient(int64_t nowMs, bool restartAfter) {
  if (dhcpPid_ <= 0) return;
  restartAfterExit_ = restartAfter;
  if (dhcpKillAtMs_ >= 0) return;
  platform_.signal(dhcpPid_, SIGTERM, true);
  dhcpKillAtMs_ = nowMs + timing_.clientStopGraceMs;
}

void IpController::scheduleRestart(int64_t nowMs) {
  restartBackoffMs_ = restartBackoffMs_ ? std::min(restartBackoffMs_ * 2, timing_.maxRestartMs) : timing_.firstRestartMs;
  dhcpRestartAtMs_ = nowMs + restartBackoffMs_;
}

bool IpController::onChildExit(pid_t pid, int status, int64_t nowMs) {
  nowMs_ = nowMs;
  if (sntp_.onChildExit(pid)) return true;
  if (pid <= 0 || pid != dhcpPid_) return false;
  dhcpPid_ = -1;
  dhcpKillAtMs_ = -1;
  readClientOutput(nowMs, true);
  const bool wasServer = dhcpServer_;
  dhcpServer_ = false;
  if (wasServer) stopAccessPoint(nowMs);
  if (stopping_) return true;
  log(std::string(wasServer ? "udhcpd" : "udhcpc") + " exited with " + describeExit(status));
  if (restartAfterExit_) {
    restartAfterExit_ = false;
    const bool flush = flushOnRestart_;
    flushOnRestart_ = false;
    restartBackoffMs_ = 0;
    startWantedClient(nowMs, flush);
    return true;
  }
  if (!linkUp_ && !accessPoint_) return true;
  if (dhcpStartedMs_ >= 0 && nowMs - dhcpStartedMs_ >= timing_.stableClientMs) restartBackoffMs_ = 0;
  scheduleRestart(nowMs);
  return true;
}

void IpController::readClientOutput(int64_t nowMs, bool exited) {
  if (clientOutput_ < 0) return;
  char buffer[1024];
  bool open = true;
  for (int round = 0; round < 8; ++round) {
    const ssize_t n = ::read(clientOutput_, buffer, sizeof(buffer));
    if (n > 0) {
      clientLog_.feed(buffer, static_cast<std::size_t>(n), nowMs);
      continue;
    }
    if (n < 0 && errno == EINTR) continue;
    open = n < 0 && errno == EAGAIN;
    break;
  }
  if (open && !exited) return;
  clientLog_.finish(nowMs);
  closeFd(clientOutput_);
}

void IpController::readEvents(int64_t nowMs) {
  char buffer[2048];
  for (int round = 0; round < 8; ++round) {
    const ssize_t n = ::read(eventRead_, buffer, sizeof(buffer));
    if (n > 0) {
      eventBuffer_.append(buffer, static_cast<std::size_t>(n));
      continue;
    }
    if (n < 0 && errno == EINTR) continue;
    break;
  }
  for (std::size_t newline; (newline = eventBuffer_.find('\n')) != std::string::npos;) {
    const std::string line = eventBuffer_.substr(0, newline);
    eventBuffer_.erase(0, newline + 1);
    handleRecord(line, nowMs);
  }
  if (eventBuffer_.size() > 4 * kMaxDhcpRecord) {
    log("discarding unterminated DHCP event data");
    eventBuffer_.clear();
  }
}

void IpController::handleRecord(const std::string& line, int64_t nowMs) {
  DhcpRecord record;
  std::string error;
  if (!parseDhcpRecord(line, kInterface, record, error)) {
    log("DHCP event rejected: " + error);
    return;
  }
  handleEvent(record, nowMs);
}

void IpController::handleEvent(const DhcpRecord& record, int64_t nowMs) {
  if (stopping_ || accessPoint_ || state_.network().link == tc002::WifiLink::AccessPoint || dhcpServer_ ||
      static_.address || (restartAfterExit_ && dhcpKillAtMs_ >= 0)) return;
  switch (record.event) {
    case DhcpEvent::Bound:
    case DhcpEvent::Renew:
      applyLease(record.lease, dhcpEventName(record.event), record.capturedMs, record.notes, nowMs);
      return;
    case DhcpEvent::Deconfig:
    case DhcpEvent::Nak:
    case DhcpEvent::LeaseFail:
      if (applier_.applied() && removeAtMs_ < 0) {
        removeAtMs_ = nowMs + timing_.leaseLossGraceMs;
        log(std::string("udhcpc reported ") + dhcpEventName(record.event) + ", keeping the address for " +
            seconds(timing_.leaseLossGraceMs));
      }
      return;
  }
}

void IpController::applyLease(DhcpLease lease, const char* source, int64_t capturedMs, const std::string& notes,
                              int64_t nowMs) {
  const uint32_t router = lease.router;
  withheldRouter_ = resolverInstalled_ ? 0 : lease.router;
  if (withheldRouter_) lease.router = 0;
  std::string error;
  const bool complete = applier_.apply(lease, error);
  if (!applier_.applied()) {
    withheldRouter_ = 0;
    log(std::string(source) + " address not applied: " + error);
    return;
  }
  if (!complete) log(std::string(source) + " address applied partially: " + error);
  if (!notes.empty()) log("lease notes: " + notes);
  if (linkUp_) removeAtMs_ = -1;
  leaseExpiresMs_ = -1;
  if (lease.leaseSeconds != kInfiniteLease) {
    int64_t age = 0;
    if (capturedMs >= 0) {
      age = platform_.monotonicMs() - capturedMs;
      if (age < 0 || age > kMaxCallbackAgeMs) age = 0;
    }
    leaseExpiresMs_ = nowMs - age + int64_t(lease.leaseSeconds) * 1000;
  }
  if (resolverInstalled_ && !platform_.resolver().write(resolvContent(lease.dns, lease.router), error))
    log("resolv.conf: " + error);
  sntp_.setFallbackServers(lease.ntp);
  log(std::string(source) + " " + formatIpv4(lease.address) + "/" +
      std::to_string(lease.prefix) + " gw " + (router ? formatIpv4(router) : "-") +
      (withheldRouter_ ? " (withheld)" : "") + " dns " + serverList(lease.dns) + " ntp " + serverList(lease.ntp) +
      (lease.leaseSeconds == kInfiniteLease ? std::string() : " lease " + std::to_string(lease.leaseSeconds) + " s"));
  publish(nowMs);
}

void IpController::removeLease(const char* reason, int64_t nowMs) {
  leaseExpiresMs_ = -1;
  if (!applier_.applied()) return;
  const std::string address = formatIpv4(applier_.lease().address);
  std::string error;
  if (!applier_.remove(error)) log("lease removal: " + error);
  withheldRouter_ = 0;
  if (resolverInstalled_ && !platform_.resolver().write(std::string(), error)) log("resolv.conf: " + error);
  log("removed " + address + " (" + reason + ")");
  publish(nowMs);
}

mdns::Identity IpController::identityFor(uint32_t address) const {
  mdns::Identity identity;
  identity.hostname = hostname_;
  identity.address = address;
  const auto port = static_cast<uint16_t>(options_.httpPort);
  identity.services.push_back({"_http._tcp", port, {}});
  identity.services.push_back(
      {"_awtrixng._tcp", port, {"id=" + macId(mac_), "name=" + hostname_, "type=awtrixng"}});
  return identity;
}

void IpController::updateServices(uint32_t address, uint8_t prefix, int ifindex, int64_t nowMs) {
  sntp_.setOnline(address != 0 && !accessPoint_, nowMs);
  if (!address || !ifindex || hostname_.empty() || !platform_.multicast()) {
    mdns_.stop();
    mdnsIdentity_ = mdns::Identity();
    mdnsRetryAtMs_ = -1;
    return;
  }
  const mdns::Identity identity = identityFor(address);
  if (sameIdentity(identity, mdnsIdentity_) && mdnsIfindex_ == ifindex && mdnsPrefix_ == prefix) return;
  if (mdnsRetryAtMs_ >= 0 && nowMs < mdnsRetryAtMs_) return;
  std::string error;
  if (platform_.startMdns(mdns_, identity, ifindex, prefix, nowMs, error)) {
    mdnsIdentity_ = identity;
    mdnsIfindex_ = ifindex;
    mdnsPrefix_ = prefix;
    mdnsRetryAtMs_ = -1;
    platform_.log("mdns", "answering for " + hostname_ + ".local");
  } else {
    mdnsIdentity_ = mdns::Identity();
    mdnsRetryAtMs_ = nowMs + std::max<int64_t>(1, timing_.mdnsRetryMs);
    platform_.log("mdns", error);
  }
}

void IpController::publish(int64_t nowMs) {
  uint32_t address = 0, gateway = 0, dns = 0;
  uint8_t prefix = 0;
  int ifindex = 0;
  if (applier_.applied()) {
    const DhcpLease& lease = applier_.lease();
    address = lease.address;
    prefix = lease.prefix;
    gateway = lease.router;
    if (resolverInstalled_) dns = !lease.dns.empty() ? lease.dns.front() : lease.router;
    ifindex = applier_.ifindex();
  }
  if (!stopping_) updateServices(address, prefix, ifindex, nowMs);
  tc002::NetworkStatus network = state_.network();
  const std::string ipv4 = address ? formatIpv4(address) : std::string();
  const std::string gatewayText = address && gateway ? formatIpv4(gateway) : std::string();
  const std::string dnsText = address && dns ? formatIpv4(dns) : std::string();
  if (network.ipv4 == ipv4 && network.gateway == gatewayText && network.dns == dnsText &&
      network.hostname == hostname_)
    return;
  network.ipv4 = ipv4;
  network.gateway = gatewayText;
  network.dns = dnsText;
  network.hostname = hostname_;
  publishing_ = true;
  state_.setNetwork(network);
  publishing_ = false;
}

void IpController::publishTime() {
  tc002::TimeStatus time = state_.time();
  if (time.synchronized == sntp_.synchronized()) return;
  time.synchronized = sntp_.synchronized();
  state_.setTime(time);
}

void IpController::pollInterest(std::vector<PollInterest>& out) const {
  if (!started_) return;
  if (eventRead_ >= 0 && !stopping_) out.push_back({eventRead_, POLLIN});
  if (clientOutput_ >= 0) out.push_back({clientOutput_, POLLIN});
  if (mdns_.fd() >= 0) out.push_back({mdns_.fd(), POLLIN});
  if (captiveDns_.fd() >= 0) out.push_back({captiveDns_.fd(), POLLIN});
  sntp_.pollInterest(out);
}

void IpController::onReady(int fd, short revents, int64_t nowMs) {
  nowMs_ = nowMs;
  if (fd < 0) return;
  if (fd == eventRead_) readEvents(nowMs);
  else if (fd == clientOutput_) readClientOutput(nowMs, false);
  else if (fd == mdns_.fd()) mdns_.onReadable(nowMs);
  else if (fd == captiveDns_.fd()) captiveDns_.onReadable();
  else sntp_.onReady(fd, revents, nowMs);
}

int64_t IpController::nextDeadlineMs() const {
  if (!started_) return -1;
  int64_t next = -1;
  const auto consider = [&next](int64_t at) {
    if (at >= 0 && (next < 0 || at < next)) next = at;
  };
  if (networkPending_) consider(nowMs_);
  consider(removeAtMs_);
  consider(leaseExpiresMs_);
  consider(dhcpRestartAtMs_);
  consider(dhcpKillAtMs_);
  consider(resolverRetryAtMs_);
  consider(mdnsRetryAtMs_);
  consider(sntp_.nextDeadlineMs());
  consider(mdns_.nextDeadlineMs());
  return next;
}

void IpController::onTime(int64_t nowMs) {
  nowMs_ = nowMs;
  if (!started_) return;
  if (networkPending_ && !stopping_) processNetwork(nowMs);
  if (resolverRetryAtMs_ >= 0 && nowMs >= resolverRetryAtMs_) installResolver(nowMs);
  if (removeAtMs_ >= 0 && nowMs >= removeAtMs_) {
    removeAtMs_ = -1;
    removeLease(linkUp_ ? "lease lost" : "link lost", nowMs);
    if (!linkUp_) stopClient(nowMs, false);
  }
  if (leaseExpiresMs_ >= 0 && nowMs >= leaseExpiresMs_) removeLease("lease expired without renewal", nowMs);
  if (!stopping_ && mdnsRetryAtMs_ >= 0 && nowMs >= mdnsRetryAtMs_) publish(nowMs);
  if (dhcpKillAtMs_ >= 0 && nowMs >= dhcpKillAtMs_) {
    dhcpKillAtMs_ = -1;
    if (dhcpPid_ > 0) {
      log(std::string(dhcpServer_ ? "udhcpd" : "udhcpc") + " ignored SIGTERM, killing it");
      platform_.signal(dhcpPid_, SIGKILL, true);
    }
  }
  if (dhcpRestartAtMs_ >= 0 && nowMs >= dhcpRestartAtMs_) {
    dhcpRestartAtMs_ = -1;
    startWantedClient(nowMs, !applier_.applied());
  }
  sntp_.onTime(nowMs);
  mdns_.onTime(nowMs);
}

void IpController::requestStop(int64_t nowMs) {
  nowMs_ = nowMs;
  if (stopping_) return;
  stopping_ = true;
  networkPending_ = false;
  removeAtMs_ = leaseExpiresMs_ = dhcpRestartAtMs_ = resolverRetryAtMs_ = mdnsRetryAtMs_ = -1;
  sntp_.stop();
  mdns_.stop();
  stopAccessPoint(nowMs);
  removeLease("stopping", nowMs);
  platform_.resolver().uninstall();
  resolverInstalled_ = false;
  stopClient(nowMs, false);
  publish(nowMs);
}

bool IpController::stopped() const {
  return (!started_ || stopping_) && dhcpPid_ <= 0 && !sntp_.childRunning();
}

void IpController::setServer(const std::string& server) {
  const std::string wanted = server.empty() ? std::string(kDefaultNtpServer) : server;
  if (!validServerName(wanted)) {
    log("rejecting NTP server \"" + server + "\"");
    return;
  }
  options_.ntpServer = wanted;
  if (started_) sntp_.setServer(wanted, nowMs_);
}

void IpController::loadStatic() {
  std::string text, error;
  DhcpRecord record;
  if (options_.staticFile.empty() || !platform_.readFile(options_.staticFile, text)) return;
  if (!parseDhcpRecord(posix::trimmed(text), kInterface, record, error) ||
      record.lease.leaseSeconds != kInfiniteLease) {
    log("ignoring " + options_.staticFile + ": " + (error.empty() ? "not a static address" : error));
    return;
  }
  static_ = record.lease;
  log("static address " + formatIpv4(static_.address) + "/" + std::to_string(static_.prefix) + " kept from the last run");
}

void IpController::setStaticAddress(const tc002::StaticAddress& address) {
  DhcpLease lease;
  std::string error;
  if (address.enabled && !staticLease(address, lease, error)) log("static address ignored (" + error + "), using DHCP");
  if (lease == static_) return;
  static_ = lease;
  log(static_.address ? "static address " + formatIpv4(static_.address) + "/" + std::to_string(static_.prefix)
                      : std::string("address from DHCP"));
  if (!options_.staticFile.empty()) {
    const bool kept = static_.address ? platform_.replaceFile(options_.staticFile, formatLease(static_, kInterface) + "\n")
                                      : platform_.removeFile(options_.staticFile);
    if (!kept) log(std::string("cannot update ") + options_.staticFile + ": " + std::strerror(errno));
  }
  if (started_ && !stopping_) addressingChanged(nowMs_);
}

void IpController::setHostname(const std::string& hostname) {
  if (!hostname.empty() && !validHostname(hostname)) {
    log("rejecting hostname \"" + hostname + "\"");
    return;
  }
  options_.hostname = hostname;
  if (started_ && !stopping_ && resolveHostname()) hostnameChanged(nowMs_);
}

}
}
}
