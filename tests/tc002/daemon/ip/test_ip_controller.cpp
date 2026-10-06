#include <csignal>
#include <map>
#include <linux/rtnetlink.h>
#include <poll.h>
#include <tuple>
#include <unistd.h>

#include "platform/tc002/daemon/ip/IpController.h"
#include "support.h"

using namespace awtrix::tc002d;
using namespace awtrix::tc002d::ip;
using awtrix::tc002::WifiLink;
using ip_test::check;
using ip_test::ipv4;

namespace {

class FakeResolver : public ResolverFile {
 public:
  bool installed = false, refuse = false;
  std::string content;
  int writes = 0, installs = 0;
  bool install(std::string& error) override {
    ++installs;
    if (refuse) error = "bind mount: Operation not permitted";
    installed = !refuse;
    return installed;
  }
  bool write(const std::string& text, std::string&) override {
    content = text;
    ++writes;
    return true;
  }
  void uninstall() override { installed = false; }
};

class NullClock : public SystemClock {
 public:
  int64_t monotonicNs() override { return 0; }
  int64_t realtimeNs() override { return 0; }
  bool step(int64_t) override { return true; }
  bool slew(int64_t) override { return true; }
};

struct Signal {
  pid_t pid;
  int number;
  bool group;
};

class FakePlatform : public IpPlatform {
 public:
  ip_test::FakeKernel fakeKernel;
  NullClock nullClock;
  FakeResolver fakeResolver;
  std::vector<SpawnRequest> spawned;
  std::vector<Signal> signals;
  std::vector<std::string> logs;
  std::vector<int> clientStderr;
  std::string kernelHostname, mac = "02:00:00:00:00:07\n";
  pid_t nextPid = 1000;
  int64_t monotonic = 0;
  bool executables = true;
  bool multicastEnabled = false, mdnsRefuse = false;
  bool apFilesRefuse = false, captiveDnsRefuse = false, spawnRefuse = false;
  int apPrepares = 0, apRemoves = 0, dnsStarts = 0;
  std::string apConfig;
  std::vector<std::tuple<std::string, uint32_t, int, uint8_t>> mdnsStarts;

  IpKernel& kernel() override { return fakeKernel; }
  SystemClock& clock() override { return nullClock; }
  ResolverFile& resolver() override { return fakeResolver; }
  ~FakePlatform() override {
    for (int fd : clientStderr)
      if (fd >= 0) ::close(fd);
  }
  pid_t spawn(const SpawnRequest& request, std::string& error) override {
    spawned.push_back(request);
    if (spawnRefuse) {
      error = "fixture spawn failure";
      return -1;
    }
    clientStderr.push_back(request.stderrFd >= 0 ? ::dup(request.stderrFd) : -1);
    return nextPid++;
  }
  void signal(pid_t pid, int number, bool group) override { signals.push_back({pid, number, group}); }
  bool setKernelHostname(const std::string& hostname) override {
    kernelHostname = hostname;
    return true;
  }
  std::string interfaceMac(const std::string&) override { return mac; }
  bool executable(const std::string&) override { return executables; }
  bool prepareDirectory(const std::string&) override { return true; }
  std::map<std::string, std::string> files;
  bool readFile(const std::string& path, std::string& out) override {
    const auto found = files.find(path);
    if (found == files.end()) return false;
    out = found->second;
    return true;
  }
  bool replaceFile(const std::string& path, const std::string& text) override {
    files[path] = text;
    return true;
  }
  bool removeFile(const std::string& path) override {
    files.erase(path);
    return true;
  }
  bool prepareAccessPoint(const std::string&, const std::string& config, std::string& error) override {
    ++apPrepares;
    apConfig = config;
    if (apFilesRefuse) error = "fixture private files failure";
    return !apFilesRefuse;
  }
  void removeAccessPoint(const std::string&) override { ++apRemoves; }
  bool startCaptiveDns(CaptiveDns&, std::string& error) override {
    ++dnsStarts;
    if (captiveDnsRefuse) error = "fixture DNS bind failure";
    return !captiveDnsRefuse;
  }
  int64_t monotonicMs() override { return monotonic; }
  bool multicast() override { return multicastEnabled; }
  bool startMdns(MdnsResponder&, const mdns::Identity& identity, int ifindex,
                 uint8_t prefix, int64_t, std::string& error) override {
    mdnsStarts.emplace_back(identity.hostname, identity.address, ifindex, prefix);
    if (mdnsRefuse) error = "mdns socket: Too many open files";
    return !mdnsRefuse;
  }
  void log(const char* component, const std::string& line) override {
    logs.push_back(std::string(component) + ": " + line);
  }

  bool logged(const std::string& text) const { return loggedTimes(text) > 0; }
  unsigned loggedTimes(const std::string& text) const {
    unsigned count = 0;
    for (const std::string& line : logs) count += line.find(text) != std::string::npos;
    return count;
  }
  bool signalled(pid_t pid, int number, bool group) const {
    for (const Signal& entry : signals)
      if (entry.pid == pid && entry.number == number && entry.group == group) return true;
    return false;
  }
};

bool hasArgument(const SpawnRequest& request, const std::string& value) {
  for (const std::string& argument : request.argv)
    if (argument == value) return true;
  return false;
}

std::string bound(const char* address, const char* lease = "3600", const char* event = "bound", const char* mono = nullptr) {
  std::string line = std::string("v=1\tevent=") + event + (mono ? std::string("\tmono=") + mono : std::string()) +
                     "\tinterface=wlan0\tip=" + address +
                     "\tsubnet=255.255.255.0\trouter=192.0.2.1\tdns=192.0.2.1 8.8.8.8\tlease=" + lease;
  return line;
}

void setLink(DeviceState& state, WifiLink link) {
  auto network = state.network();
  network.link = link;
  network.ssid = "Home";
  network.rssi = -55;
  state.setNetwork(network);
}

IpOptions controllerOptions() {
  IpOptions options;
  options.runDir = "/tmp/awtrix-tc002d";
  options.udhcpc = "/data/awtrix-ng/current/bin/udhcpc";
  options.dhcpCallback = "/data/awtrix-ng/current/bin/dhcp-callback";
  options.ntpServer = "127.0.0.1";
  return options;
}

SntpTiming quietSntp() {
  SntpTiming timing;
  timing.port = 9;
  return timing;
}

void seedStockLease(ip_test::FakeKernel& kernel) {
  KernelAddress stock;
  stock.ifindex = 3;
  stock.address = ipv4("192.0.2.110");
  stock.prefix = 24;
  kernel.addAddress(stock);
  KernelRoute route;
  route.ifindex = 3;
  route.gateway = ipv4("192.0.2.1");
  route.table = RT_TABLE_MAIN;
  route.protocol = RTPROT_BOOT;
  kernel.routeTable.push_back(route);
}

void testLifecycle() {
  DeviceState state;
  FakePlatform platform;
  seedStockLease(platform.fakeKernel);
  IpController controller(state, controllerOptions(), platform, IpTiming(), quietSntp());
  int64_t now = 1000;
  check(controller.start(now), "start");
  check(platform.fakeResolver.installed, "resolv.conf bound at start");
  check(platform.kernelHostname == "awtrixng-000007" && state.network().hostname == "awtrixng-000007",
        "default hostname from the interface MAC");
  controller.onTime(now);
  check(platform.spawned.empty() && platform.fakeKernel.addressesOn(3) == 1, "nothing happens before the link is up");

  setLink(state, WifiLink::Connected);
  check(controller.nextDeadlineMs() == now, "network change is handled on the next tick");
  controller.onTime(now);
  check(platform.fakeKernel.addressesOn(3) == 0 && platform.fakeKernel.defaultRoutes() == 0,
        "stale stock lease cleared on link-up");
  check(platform.spawned.size() == 1, "udhcpc started");
  const SpawnRequest& first = platform.spawned.at(0);
  const std::vector<std::string> expected = {"udhcpc", "-f", "-i", "wlan0",
                                             "-s", controllerOptions().dhcpCallback, "-t", "4",
                                             "-T", "3", "-A", "10", "-V", "",
                                             "-x", "hostname:awtrixng-000007"};
  check(first.path == controllerOptions().udhcpc && first.argv == expected && first.passAs == 198 && first.passFd >= 0,
        "udhcpc command line and event descriptor");
  check(!hasArgument(first, "-c") && !hasArgument(first, "-C") && !hasArgument(first, "-O"),
        "no vendor class, the default MAC client id, no extra requests");
  const pid_t client = controller.dhcpClient();

  controller.handleRecord("v=1\tevent=deconfig\tinterface=wlan0", now);
  check(!controller.leaseApplied() && controller.nextDeadlineMs() < 0, "initial deconfig is harmless");

  platform.monotonic = 50000;
  controller.handleRecord(bound("192.0.2.57", "3600", "bound", "45000"), now);
  check(controller.leaseApplied() && platform.fakeKernel.addressesOn(3) == 1 &&
            platform.fakeKernel.defaultRoutesVia(ipv4("192.0.2.1")) == 1,
        "lease applied to the kernel");
  const auto& network = state.network();
  check(network.ipv4 == "192.0.2.57" && network.gateway == "192.0.2.1" && network.dns == "192.0.2.1" &&
            network.ssid == "Home" && network.rssi == -55 && network.link == WifiLink::Connected,
        "published own fields, Wi-Fi fields untouched");
  check(platform.fakeResolver.content == "nameserver 192.0.2.1\nnameserver 8.8.8.8\n", "resolv.conf rewritten");

  now += 1000;
  setLink(state, WifiLink::Disconnected);
  controller.onTime(now);
  controller.onTime(now + 4000);
  check(controller.leaseApplied() && !platform.signalled(client, SIGTERM, true), "link loss starts a grace period");
  now += 5000;
  setLink(state, WifiLink::Connected);
  controller.onTime(now);
  check(platform.signalled(client, SIGUSR1, false) && platform.spawned.size() == 1 && controller.leaseApplied(),
        "short roam renews instead of restarting");
  now += 30000;
  controller.onTime(now);
  check(controller.leaseApplied(), "grace cancelled by the returning link");

  controller.handleRecord("v=1\tevent=nak\tinterface=wlan0", now);
  controller.handleRecord("v=1\tevent=deconfig\tinterface=wlan0", now);
  controller.onTime(now + 500);
  check(controller.leaseApplied(), "lease loss waits for a new lease");
  controller.handleRecord(bound("192.0.2.58"), now + 1000);
  now += 21000;
  controller.onTime(now);
  check(controller.leaseApplied() && state.network().ipv4 == "192.0.2.58" &&
            platform.fakeKernel.addressesOn(3) == 1,
        "new lease replaces the old address");

  setLink(state, WifiLink::Disconnected);
  controller.onTime(now);
  now += 20001;
  controller.onTime(now);
  check(!controller.leaseApplied() && platform.fakeKernel.addressesOn(3) == 0 &&
            platform.fakeKernel.defaultRoutes() == 0 && state.network().ipv4.empty() && state.network().gateway.empty(),
        "lease removed after the grace period");
  check(platform.signalled(client, SIGTERM, true), "udhcpc stopped while the link is down");
  check(platform.fakeResolver.content.empty(), "no nameserver without a lease");
  check(controller.onChildExit(client, 0, now) && controller.dhcpClient() < 0 && platform.spawned.size() == 1,
        "no restart while the link is down");
  check(!controller.onChildExit(4242, 0, now), "foreign children are not claimed");

  setLink(state, WifiLink::Connected);
  controller.onTime(now);
  check(platform.spawned.size() == 2 && !hasArgument(platform.spawned.back(), "-r"), "fresh client after the link returns");
  const pid_t second = controller.dhcpClient();
  controller.handleRecord(bound("192.0.2.57"), now);

  check(controller.onChildExit(second, SIGSEGV, now), "crashed client claimed");
  controller.onTime(now + 999);
  check(platform.spawned.size() == 2, "restart waits for the backoff");
  controller.onTime(now + 1000);
  check(platform.spawned.size() == 3 && hasArgument(platform.spawned.back(), "-r") &&
            hasArgument(platform.spawned.back(), "192.0.2.57") && controller.leaseApplied(),
        "restart keeps the address and requests it again");
  const pid_t third = controller.dhcpClient();
  controller.onChildExit(third, 1 << 8, now + 1500);
  controller.onTime(now + 3499);
  check(platform.spawned.size() == 3, "backoff doubles");
  controller.onTime(now + 3500);
  check(platform.spawned.size() == 4, "restart after the doubled backoff");
  const pid_t fourth = controller.dhcpClient();
  now += 4000;

  controller.setHostname("bad_name");
  check(platform.kernelHostname == "awtrixng-000007", "invalid hostname rejected");
  controller.setHostname("kitchen-clock");
  check(platform.kernelHostname == "kitchen-clock" && state.network().hostname == "kitchen-clock", "hostname applied");
  check(platform.signalled(fourth, SIGTERM, true), "udhcpc restarted for the new hostname");
  controller.onChildExit(fourth, 0, now);
  check(platform.spawned.size() == 5 && hasArgument(platform.spawned.back(), "hostname:kitchen-clock") &&
            hasArgument(platform.spawned.back(), "-r"),
        "new client sends the hostname and asks for the same address");
  controller.setHostname("");
  check(platform.kernelHostname == "awtrixng-000007", "empty hostname returns to the default");
  controller.onChildExit(controller.dhcpClient(), 0, now);

  controller.handleRecord("v=1\tevent=bound\tinterface=wlan0\tip=192.0.2.57\tsubnet=255.255.255.0\tlease=3600",
                          now);
  check(controller.leaseApplied() && platform.fakeResolver.content.empty() && state.network().dns.empty(),
        "a lease naming neither DNS nor router leaves no nameserver behind");
  controller.handleRecord(bound("192.0.2.57", "10"), now);
  controller.onTime(now + 10001);
  check(!controller.leaseApplied() && state.network().ipv4.empty(), "unrenewed lease expires");

  controller.handleRecord(bound("192.0.2.57"), now);
  controller.handleRecord("v=1\tevent=bound\tinterface=eth0\tip=1.2.3.4", now);
  controller.handleRecord("garbage", now);
  check(state.network().ipv4 == "192.0.2.57", "rejected records change nothing");

  const pid_t running = controller.dhcpClient();
  controller.requestStop(now);
  check(!controller.leaseApplied() && platform.fakeKernel.addressesOn(3) == 0 && !platform.fakeResolver.installed &&
            state.network().ipv4.empty(),
        "stop removes the lease and the resolv.conf bind");
  check(platform.signalled(running, SIGTERM, true) && !controller.stopped(), "stop waits for udhcpc");
  controller.onTime(now + 2000);
  check(platform.signalled(running, SIGKILL, true), "udhcpc killed after the stop grace");
  controller.onChildExit(running, SIGKILL, now + 2001);
  check(controller.stopped() && platform.spawned.size() == 6, "stopped without a restart");
}

void testResolverNotBound() {
  DeviceState state;
  FakePlatform platform;
  platform.fakeResolver.refuse = true;
  seedStockLease(platform.fakeKernel);
  IpController controller(state, controllerOptions(), platform, IpTiming(), quietSntp());
  check(controller.start(0), "start without a resolv.conf bind");
  check(platform.logged("resolv.conf: bind mount: Operation not permitted; no DNS and no default route"),
        "failed bind reported");
  setLink(state, WifiLink::Connected);
  controller.onTime(0);
  controller.handleRecord(bound("192.0.2.57"), 0);
  check(controller.leaseApplied() && platform.fakeKernel.addressesOn(3) == 1 &&
            platform.fakeKernel.defaultRoutes() == 0,
        "lease applied for the LAN only, the stock route flushed");
  check(state.network().ipv4 == "192.0.2.57" && state.network().gateway.empty() && state.network().dns.empty(),
        "no gateway and no DNS published");
  check(platform.fakeResolver.writes == 0 && platform.logged("gw 192.0.2.1 (withheld)"), "router withheld");
  controller.onTime(9999);
  check(platform.fakeResolver.installs == 1, "bind not retried before its time");
  controller.onTime(10000);
  check(platform.fakeResolver.installs == 2 && platform.loggedTimes("no DNS and no default route") == 1,
        "retry failed again, reported once");
  controller.handleRecord(bound("192.0.2.57", "3600", "renew"), 15000);
  check(platform.fakeKernel.defaultRoutes() == 0, "a renewal does not bring the route back");

  platform.fakeResolver.refuse = false;
  controller.onTime(20000);
  check(platform.fakeResolver.installed && platform.logged("resolv.conf bound"), "bind retried until it holds");
  check(platform.fakeKernel.defaultRoutesVia(ipv4("192.0.2.1")) == 1 &&
            platform.fakeResolver.content == "nameserver 192.0.2.1\nnameserver 8.8.8.8\n" &&
            state.network().gateway == "192.0.2.1" && state.network().dns == "192.0.2.1",
        "route and DNS follow once our file is bound");
  controller.handleRecord(bound("192.0.2.57", "3600", "renew"), 21000);
  controller.onTime(30000);
  check(platform.fakeKernel.defaultRoutesVia(ipv4("192.0.2.1")) == 1 && platform.fakeResolver.installs == 3,
        "renewals keep the route, no further retries");
  controller.requestStop(22000);
  check(!platform.fakeResolver.installed, "stop unbinds");
}

void testCapturedLeaseExpiry() {
  DeviceState state;
  FakePlatform platform;
  IpController controller(state, controllerOptions(), platform, IpTiming(), quietSntp());
  controller.start(0);
  setLink(state, WifiLink::Connected);
  controller.onTime(0);
  platform.monotonic = 50000;
  controller.handleRecord(bound("192.0.2.57", "60", "bound", "45000"), 100000);
  controller.onTime(154999);
  check(controller.leaseApplied(), "lease valid until capture time plus lease");
  controller.onTime(155000);
  check(!controller.leaseApplied(), "time spent before delivery counts against the lease");
  platform.monotonic = 500000;
  controller.handleRecord(bound("192.0.2.57", "60", "bound", "45000"), 200000);
  controller.onTime(259999);
  check(controller.leaseApplied(), "implausibly old capture times are not trusted");
}

void testClientOutputLogged() {
  DeviceState state;
  FakePlatform platform;
  IpController controller(state, controllerOptions(), platform, IpTiming(), quietSntp());
  controller.start(0);
  setLink(state, WifiLink::Connected);
  controller.onTime(0);
  check(platform.spawned.size() == 1 && platform.spawned[0].discardStdout && platform.clientStderr.at(0) >= 0,
        "udhcpc stdout discarded, stderr piped to the daemon");
  const std::string text = "udhcpc: broadcasting discover\nudhcpc: bind: Address in use\nudhcpc: dying";
  check(::write(platform.clientStderr[0], text.data(), text.size()) == static_cast<ssize_t>(text.size()),
        "client output written");
  std::vector<PollInterest> interest;
  controller.pollInterest(interest);
  for (const PollInterest& entry : interest) controller.onReady(entry.fd, POLLIN, 10);
  check(platform.logged("udhcpc: bind: Address in use") && !platform.logged("broadcasting discover") &&
            !platform.logged("dying"),
        "client errors logged line by line, routine lines dropped");
  ::close(platform.clientStderr[0]);
  platform.clientStderr[0] = -1;
  controller.onChildExit(controller.dhcpClient(), 1 << 8, 20);
  check(platform.logged("udhcpc: dying"), "last partial line logged when the client exits");
  controller.requestStop(30);
}

void testInterfaceRecreated() {
  DeviceState state;
  FakePlatform platform;
  IpController controller(state, controllerOptions(), platform, IpTiming(), quietSntp());
  int64_t now = 0;
  controller.start(now);
  setLink(state, WifiLink::Connected);
  controller.onTime(now);
  const pid_t client = controller.dhcpClient();
  controller.handleRecord(bound("192.0.2.57"), now);
  setLink(state, WifiLink::Disconnected);
  controller.onTime(now);
  platform.fakeKernel.interfaces["wlan0"] = 4;
  platform.fakeKernel.addressTable.clear();
  platform.fakeKernel.routeTable.clear();
  setLink(state, WifiLink::Connected);
  controller.onTime(now + 3000);
  check(platform.signalled(client, SIGTERM, true) && !platform.signalled(client, SIGUSR1, false),
        "recreated interface restarts udhcpc");
  controller.onChildExit(client, 0, now + 3100);
  check(platform.spawned.size() == 2 && !hasArgument(platform.spawned.back(), "-r"), "fresh client on the new interface");

  platform.executables = false;
  controller.onChildExit(controller.dhcpClient(), 0, now + 4000);
  controller.onTime(now + 5000);
  controller.onTime(now + 6000);
  check(platform.spawned.size() == 2 && platform.logged("not executable"),
        "missing binaries retry later");
  platform.executables = true;
  controller.onTime(now + 20000);
  check(platform.spawned.size() == 3, "client starts once the binaries are there");
  const pid_t last = controller.dhcpClient();
  controller.requestStop(now + 21000);
  check(!controller.stopped() && platform.signalled(last, SIGTERM, true), "stop signals the client");
  controller.onChildExit(last, 0, now + 21100);
  check(controller.stopped(), "stopped once the client is reaped");
}

void testConfiguredHostname() {
  DeviceState state;
  FakePlatform platform;
  IpOptions options = controllerOptions();
  options.hostname = "desk";
  IpController controller(state, options, platform, IpTiming(), quietSntp());
  check(controller.start(0), "start with a configured hostname");
  check(platform.kernelHostname == "desk" && state.network().hostname == "desk",
        "configured hostname applied instead of the MAC default");
  controller.setHostname("desk2");
  check(platform.kernelHostname == "desk2" && state.network().hostname == "desk2" && platform.signals.empty(),
        "a hostname change without a running client restarts nothing");
  setLink(state, WifiLink::Connected);
  controller.onTime(10);
  check(platform.spawned.size() == 1 && hasArgument(platform.spawned[0], "hostname:desk2"),
        "the first client announces the configured hostname");
  controller.requestStop(20);
}

awtrix::tc002::StaticAddress fixedAddress(const char* address) {
  awtrix::tc002::StaticAddress fixed;
  fixed.enabled = true;
  fixed.ip = address;
  fixed.subnet = "255.255.255.0";
  fixed.gateway = "192.0.2.1";
  return fixed;
}

constexpr const char* kStaticFile = "/data/awtrix-ng/state/static-address";

IpOptions staticOptions() {
  IpOptions options = controllerOptions();
  options.staticFile = kStaticFile;
  return options;
}

void testStaticAddress() {
  DeviceState state;
  FakePlatform platform;
  seedStockLease(platform.fakeKernel);
  IpController controller(state, staticOptions(), platform, IpTiming(), quietSntp());
  controller.setStaticAddress(fixedAddress("192.0.2.50"));
  check(platform.files.count(kStaticFile) == 1, "static address kept for the next start");
  check(controller.start(0), "start with a static address");
  setLink(state, WifiLink::Connected);
  controller.onTime(10);
  check(platform.spawned.empty() && controller.leaseApplied(), "a static address replaces udhcpc");
  check(platform.fakeKernel.addressesOn(3) == 1 && platform.fakeKernel.defaultRoutesVia(ipv4("192.0.2.1")) == 1,
        "static address and default route in the kernel, stock lease gone");
  check(state.network().ipv4 == "192.0.2.50" && state.network().gateway == "192.0.2.1" &&
            state.network().dns == "192.0.2.1" && platform.fakeResolver.content == "nameserver 192.0.2.1\n",
        "static address published, the gateway answers DNS");
  controller.handleRecord(bound("192.0.2.77"), 20);
  check(state.network().ipv4 == "192.0.2.50", "stray DHCP records ignored");

  auto changed = fixedAddress("192.0.2.60");
  changed.dns1 = "9.9.9.9";
  changed.dns2 = "1.1.1.1";
  controller.setStaticAddress(changed);
  check(state.network().ipv4 == "192.0.2.60" && platform.fakeKernel.addressesOn(3) == 1 &&
            platform.fakeResolver.content == "nameserver 9.9.9.9\nnameserver 1.1.1.1\n",
        "a changed static address applies at once");

  controller.setStaticAddress(awtrix::tc002::StaticAddress());
  check(platform.spawned.size() == 1 && !controller.leaseApplied() && state.network().ipv4.empty(),
        "switching to DHCP starts udhcpc");
  controller.handleRecord(bound("192.0.2.77"), 30);
  check(state.network().ipv4 == "192.0.2.77", "DHCP lease applied");

  const pid_t client = controller.dhcpClient();
  controller.setStaticAddress(fixedAddress("192.0.2.50"));
  check(platform.signalled(client, SIGTERM, true) && !controller.leaseApplied(), "switching to static stops udhcpc");
  controller.onChildExit(client, 0, 40);
  check(controller.dhcpClient() <= 0 && platform.spawned.size() == 1 && state.network().ipv4 == "192.0.2.50",
        "static address applied once udhcpc is gone");

  const std::size_t operations = platform.fakeKernel.operations.size();
  const unsigned applied = platform.loggedTimes("static 192.0.2.50/24");
  setLink(state, WifiLink::Disconnected);
  controller.onTime(50);
  setLink(state, WifiLink::Connected);
  controller.onTime(60);
  check(controller.leaseApplied() && platform.fakeKernel.operations.size() == operations &&
            platform.loggedTimes("static 192.0.2.50/24") == applied,
        "a short roam keeps the static address untouched");
  setLink(state, WifiLink::Disconnected);
  controller.onTime(70);
  controller.onTime(70 + IpTiming().linkLossGraceMs);
  check(!controller.leaseApplied() && state.network().ipv4.empty(), "a long link loss removes the static address");
  setLink(state, WifiLink::Connected);
  const int64_t back = 80 + IpTiming().linkLossGraceMs;
  controller.onTime(back);
  check(controller.leaseApplied() && state.network().ipv4 == "192.0.2.50" && platform.spawned.size() == 1,
        "the static address returns with the link");

  controller.setStaticAddress(fixedAddress("192.0.2.255"));
  check(platform.logged("static address ignored (address is not a host in its subnet)") &&
            platform.spawned.size() == 2 && !controller.leaseApplied() && platform.files.count(kStaticFile) == 0,
        "an unusable static address falls back to DHCP and is forgotten");
  controller.requestStop(back + 10);
}

void testStaticAddressFromLastRun() {
  DeviceState state;
  FakePlatform platform;
  platform.files[kStaticFile] =
      "v=1\tevent=bound\tinterface=wlan0\tip=192.0.2.50\tsubnet=255.255.255.0\trouter=192.0.2.1\t"
      "dns=192.0.2.1\tlease=4294967295\n";
  IpController controller(state, staticOptions(), platform, IpTiming(), quietSntp());
  check(controller.start(0), "start");
  setLink(state, WifiLink::Connected);
  controller.onTime(10);
  check(platform.spawned.empty() && state.network().ipv4 == "192.0.2.50",
        "a boot uses the kept static address before the runtime reports in");
  controller.setStaticAddress(fixedAddress("192.0.2.50"));
  check(platform.loggedTimes("static 192.0.2.50/24") == 1, "the runtime confirming it changes nothing");
  controller.setStaticAddress(awtrix::tc002::StaticAddress());
  check(platform.files.count(kStaticFile) == 0 && platform.spawned.size() == 1, "DHCP from the runtime wins");
  controller.requestStop(20);

  FakePlatform stale;
  stale.files[kStaticFile] = "v=1\tevent=bound\tinterface=wlan0\tip=192.0.2.50\tsubnet=255.255.255.0\tlease=60\n";
  IpController ignoring(state, staticOptions(), stale, IpTiming(), quietSntp());
  check(stale.logged("not a static address"), "a file that is not a static address is ignored");
}

void testStaticAddressFailures() {
  DeviceState state;
  FakePlatform platform;
  platform.fakeKernel.addAddressError = EPERM;
  IpController controller(state, staticOptions(), platform, IpTiming(), quietSntp());
  controller.setStaticAddress(fixedAddress("192.0.2.50"));
  check(controller.start(0), "start");
  setLink(state, WifiLink::Connected);
  controller.onTime(10);
  check(!controller.leaseApplied() && platform.logged("static address not applied") &&
            controller.nextDeadlineMs() == 10 + IpTiming().firstRestartMs,
        "a refused static address is retried");
  platform.fakeKernel.addAddressError = 0;
  controller.onTime(10 + IpTiming().firstRestartMs);
  check(controller.leaseApplied() && state.network().ipv4 == "192.0.2.50", "the retry applies it");

  setLink(state, WifiLink::AccessPoint);
  controller.onTime(2000);
  controller.setStaticAddress(fixedAddress("192.0.2.60"));
  check(state.network().ipv4 == "192.168.4.1", "the setup access point keeps its own address");
  const pid_t server = controller.dhcpClient();
  setLink(state, WifiLink::Connected);
  controller.onTime(2100);
  controller.onChildExit(server, 0, 2200);
  check(controller.dhcpClient() <= 0 && state.network().ipv4 == "192.0.2.60",
        "the new static address follows the access point");
  controller.requestStop(2300);
}

void testMisconfiguration() {
  DeviceState state;
  FakePlatform platform;
  IpOptions options = controllerOptions();
  options.udhcpc.clear();
  IpController controller(state, options, platform, IpTiming(), quietSntp());
  check(!controller.start(0), "a controller without a DHCP client refuses to start");
  check(controller.stopped(), "unstarted controller counts as stopped");
}

void testFailedRenewalKeepsOwnership() {
  for (int failure : {EIO, ETIMEDOUT}) {
    DeviceState state;
    FakePlatform platform;
    IpController controller(state, controllerOptions(), platform, IpTiming(), quietSntp());
    check(controller.start(0), "start for failed lease renewal");
    setLink(state, WifiLink::Connected);
    controller.onTime(0);
    controller.handleRecord(bound("192.0.2.57"), 0);
    platform.fakeKernel.addAddressError = failure;
    controller.handleRecord(bound("192.0.2.57", "3600", "renew"), 1000);
    check(controller.leaseApplied() && state.network().ipv4 == "192.0.2.57" &&
              state.network().gateway == "192.0.2.1" && platform.fakeKernel.addressesOn(3) == 1,
          "failed renewal preserves the installed lease and published address");
    controller.requestStop(2000);
    check(platform.fakeKernel.addressesOn(3) == 0 && platform.fakeKernel.defaultRoutes() == 0,
          "stop removes the confirmed address and route after a failed renewal");
  }
}

void testMdnsPrefixAndRetry() {
  DeviceState state;
  FakePlatform platform;
  platform.multicastEnabled = true;
  platform.mdnsRefuse = true;
  IpTiming timing;
  timing.mdnsRetryMs = 1000;
  IpController controller(state, controllerOptions(), platform, timing, quietSntp());
  check(controller.start(0), "start with failing mDNS socket setup");
  setLink(state, WifiLink::Connected);
  controller.onTime(0);
  controller.handleRecord(bound("192.0.2.57"), 0);
  check(platform.mdnsStarts.size() == 1, "first lease attempts mDNS startup");
  controller.handleRecord(bound("192.0.2.57", "3600", "renew"), 500);
  controller.onTime(999);
  check(platform.mdnsStarts.size() == 1 && controller.nextDeadlineMs() == timing.mdnsRetryMs,
        "failed mDNS setup schedules a bounded retry without repeated starts");
  controller.onTime(1000);
  check(platform.mdnsStarts.size() == 2 && controller.nextDeadlineMs() == 2 * timing.mdnsRetryMs,
        "unchanged lease retries mDNS setup after the deadline");
  platform.mdnsRefuse = false;
  controller.onTime(2000);
  check(platform.mdnsStarts.size() == 3 && platform.logged("answering for awtrixng-000007.local"),
        "mDNS recovers without another DHCP or hostname change");
  controller.handleRecord(bound("192.0.2.57", "3600", "renew"), 2500);
  controller.onTime(3000);
  check(platform.mdnsStarts.size() == 3, "successful identity is reused without a further retry");
  controller.handleRecord("v=1\tevent=renew\tinterface=wlan0\tip=192.0.2.57\tsubnet=255.255.0.0\t"
                          "router=192.0.2.1\tlease=3600", 4000);
  check(platform.mdnsStarts.size() == 4 && std::get<3>(platform.mdnsStarts.back()) == 16,
        "same-address renewal restarts mDNS with the new subnet prefix");
  platform.mdnsRefuse = true;
  controller.setHostname("desk");
  check(platform.mdnsStarts.size() == 5, "changed hostname attempts mDNS setup");
  controller.requestStop(4500);
  controller.onTime(5000);
  check(platform.mdnsStarts.size() == 5, "stop cancels a pending mDNS retry");
}

void testAccessPointLifecycle() {
  DeviceState state;
  FakePlatform platform;
  seedStockLease(platform.fakeKernel);
  IpController controller(state, controllerOptions(), platform, IpTiming(), quietSntp());
  check(controller.start(0), "AP controller starts");
  setLink(state, WifiLink::AccessPoint);
  controller.onTime(0);
  const pid_t server = controller.dhcpClient();
  check(server > 0 && platform.spawned.size() == 1 && platform.spawned.back().argv ==
            std::vector<std::string>({"udhcpd", "-f", "/tmp/awtrix-tc002d/access-point/udhcpd.conf"}) &&
            platform.spawned.back().path == controllerOptions().udhcpc && platform.spawned.back().passFd < 0,
        "AP dispatches the installed BusyBox as udhcpd without a station event descriptor");
  check(state.network().ipv4 == "192.168.4.1" && state.network().gateway.empty() &&
            platform.fakeKernel.addressesOn(3) == 1 && platform.fakeKernel.defaultRoutes() == 0 &&
            platform.fakeKernel.addressTable.back().prefix == 24 && platform.dnsStarts == 1,
        "AP replaces stale station state with its private subnet and captive DNS");
  check(platform.apConfig.find("max_leases 8\n") != std::string::npos &&
            platform.apConfig.find("option dns 192.168.4.1\n") != std::string::npos &&
            platform.apConfig.find("option router 192.168.4.1\n") != std::string::npos &&
            platform.apConfig.find("lease_file /tmp/awtrix-tc002d/access-point/leases\n") != std::string::npos,
        "AP DHCP config bounds leases and advertises the local portal and DNS");
  controller.handleRecord(bound("192.0.2.57"), 1);
  controller.setHostname("setup");
  controller.onTime(600001);
  check(state.network().ipv4 == "192.168.4.1" && platform.spawned.size() == 1 && platform.signals.empty(),
        "late station lease and hostname updates cannot replace or expire AP state");
  setLink(state, WifiLink::Connected);
  controller.onTime(600002);
  check(state.network().ipv4.empty() && platform.fakeKernel.addressesOn(3) == 0 &&
            platform.signalled(server, SIGTERM, true) && platform.spawned.size() == 1,
        "leaving AP removes the address and waits for DHCP server termination");
  controller.handleRecord(bound("192.0.2.58"), 600003);
  check(state.network().ipv4.empty(), "station callbacks are ignored while the AP server is stopping");
  controller.onChildExit(server, 0, 600004);
  check(platform.spawned.size() == 2 && platform.spawned.back().argv.front() == "udhcpc" &&
            platform.apRemoves > 0, "station DHCP starts only after the server is reaped and files are cleared");
  controller.handleRecord(bound("192.0.2.59"), 600005);
  check(state.network().ipv4 == "192.0.2.59", "station DHCP works after AP teardown");
  const pid_t client = controller.dhcpClient();
  setLink(state, WifiLink::AccessPoint);
  controller.onTime(600006);
  check(state.network().ipv4.empty() && platform.fakeKernel.defaultRoutes() == 0 &&
            platform.fakeResolver.content.empty() && platform.signalled(client, SIGTERM, true) &&
            platform.spawned.size() == 2, "router loss clears station lease, route and resolver before AP DHCP");
  controller.handleRecord(bound("192.0.2.60"), 600007);
  controller.onChildExit(client, 0, 600008);
  check(state.network().ipv4 == "192.168.4.1" && platform.spawned.size() == 3,
        "AP recovers after the station child exits");
  const pid_t secondServer = controller.dhcpClient();
  controller.requestStop(600009);
  check(state.network().ipv4.empty() && !controller.stopped() &&
            platform.signalled(secondServer, SIGTERM, true), "shutdown removes AP networking and stops DHCP");
  controller.onTime(602009);
  check(platform.signalled(secondServer, SIGKILL, true), "unresponsive AP server is killed at the shutdown deadline");
  controller.onChildExit(secondServer, 0, 602010);
  check(controller.stopped() && platform.fakeKernel.addressesOn(3) == 0,
        "AP shutdown finishes after child reap without an address leak");
}

void testAccessPointFailures() {
  for (unsigned failure = 0; failure < 4; ++failure) {
    DeviceState state;
    FakePlatform platform;
    platform.apFilesRefuse = failure == 0;
    platform.captiveDnsRefuse = failure == 1;
    platform.spawnRefuse = failure == 2;
    platform.fakeKernel.addAddressError = failure == 3 ? EIO : 0;
    IpController controller(state, controllerOptions(), platform, IpTiming(), quietSntp());
    check(controller.start(0), "controller accepts AP recovery fixture");
    setLink(state, WifiLink::AccessPoint);
    controller.onTime(0);
    check(controller.dhcpClient() <= 0 && state.network().ipv4.empty() &&
              platform.fakeKernel.addressesOn(3) == 0 && controller.nextDeadlineMs() == IpTiming().firstRestartMs,
          "AP setup failure rolls back address and schedules bounded retry");
    platform.apFilesRefuse = platform.captiveDnsRefuse = platform.spawnRefuse = false;
    platform.fakeKernel.addAddressError = 0;
    controller.onTime(999);
    check(controller.dhcpClient() <= 0, "AP failure does not busy loop");
    controller.onTime(1000);
    check(controller.dhcpClient() > 0 && state.network().ipv4 == "192.168.4.1", "AP setup retry recovers");
    const pid_t child = controller.dhcpClient();
    controller.onChildExit(child, 1 << 8, 1001);
    check(state.network().ipv4.empty() && platform.fakeKernel.addressesOn(3) == 0,
          "unexpected DHCP server exit tears down incomplete AP services");
    setLink(state, WifiLink::Disconnected);
    controller.onTime(1002);
    controller.onTime(10000);
    check(controller.dhcpClient() <= 0 && controller.nextDeadlineMs() < 0,
          "leaving failed AP cancels retry rather than running a server in station mode");
    controller.requestStop(10001);
  }
}

}

int main() {
  testLifecycle();
  testResolverNotBound();
  testCapturedLeaseExpiry();
  testClientOutputLogged();
  testInterfaceRecreated();
  testConfiguredHostname();
  testStaticAddress();
  testStaticAddressFromLastRun();
  testStaticAddressFailures();
  testMisconfiguration();
  testFailedRenewalKeepsOwnership();
  testMdnsPrefixAndRetry();
  testAccessPointLifecycle();
  testAccessPointFailures();
  return ip_test::finish("tc002d-ip-controller");
}
