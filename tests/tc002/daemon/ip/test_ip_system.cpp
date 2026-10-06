#include <algorithm>
#include <arpa/inet.h>
#include <cerrno>
#include <csignal>
#include <cstring>
#include <ctime>
#include <fcntl.h>
#include <functional>
#include <net/ethernet.h>
#include <net/if.h>
#include <netpacket/packet.h>
#include <linux/if_link.h>
#include <linux/netlink.h>
#include <linux/rtnetlink.h>
#include <linux/veth.h>
#include <netinet/in.h>
#include <poll.h>
#include <sched.h>
#include <sys/mount.h>
#include <sys/stat.h>
#include <sys/socket.h>
#include <sys/utsname.h>
#include <sys/wait.h>
#include <unistd.h>

#include <fstream>
#include <map>
#include <sstream>

#include "platform/tc002/daemon/ip/IpController.h"
#include "platform/tc002/daemon/ip/DhcpRecordFormat.h"
#include "platform/tc002/daemon/ip/LeaseApplier.h"
#include "platform/tc002/daemon/ip/Mdns.h"
#include "platform/tc002/daemon/ip/ResolvConf.h"
#include "platform/tc002/daemon/ip/SystemPlatform.h"
#include "support.h"

// Runs as root in a throwaway network and mount namespace (the privileged container):
// real rtnetlink on a dummy wlan0, the resolv.conf bind mount, the controller with a fake
// udhcpc and, when a host BusyBox with udhcpd is given, a real DHCP exchange over veth.
using namespace awtrix::tc002d;
using namespace awtrix::tc002d::ip;
using awtrix::tc002::WifiLink;
using ip_test::check;
using ip_test::ipv4;

namespace {

int64_t nowMs() {
  timespec now{};
  clock_gettime(CLOCK_MONOTONIC, &now);
  return int64_t(now.tv_sec) * 1000 + now.tv_nsec / 1000000;
}

std::string readFile(const std::string& path) {
  std::ifstream in(path);
  std::stringstream out;
  out << in.rdbuf();
  return out.str();
}

class LinkRequest {
 public:
  LinkRequest(uint16_t type, uint16_t flags) {
    nlmsghdr header{};
    header.nlmsg_type = type;
    header.nlmsg_flags = static_cast<uint16_t>(NLM_F_REQUEST | NLM_F_ACK | flags);
    header.nlmsg_seq = 1;
    append(&header, sizeof(header));
    ifinfomsg body{};
    body.ifi_family = AF_UNSPEC;
    append(&body, sizeof(body));
  }
  ifinfomsg& body() { return *reinterpret_cast<ifinfomsg*>(bytes_ + NLMSG_HDRLEN); }
  void attribute(uint16_t type, const void* data, std::size_t size) {
    align();
    rtattr header{};
    header.rta_type = type;
    header.rta_len = static_cast<unsigned short>(RTA_LENGTH(size));
    append(&header, sizeof(header));
    append(data, size);
  }
  void text(uint16_t type, const char* value) { attribute(type, value, std::strlen(value) + 1); }
  std::size_t open(uint16_t type) {
    align();
    const std::size_t at = size_;
    attribute(type, nullptr, 0);
    return at;
  }
  void close(std::size_t at) {
    align();
    const auto length = static_cast<unsigned short>(size_ - at);
    std::memcpy(bytes_ + at, &length, sizeof(length));
  }
  void raw(const void* data, std::size_t size) {
    align();
    append(data, size);
  }
  bool send() {
    align();
    const uint32_t total = static_cast<uint32_t>(size_);
    std::memcpy(bytes_, &total, sizeof(total));
    const int fd = ::socket(AF_NETLINK, SOCK_RAW | SOCK_CLOEXEC, NETLINK_ROUTE);
    sockaddr_nl kernel{};
    kernel.nl_family = AF_NETLINK;
    bool okay = fd >= 0 && ::sendto(fd, bytes_, size_, 0, reinterpret_cast<sockaddr*>(&kernel), sizeof(kernel)) ==
                               static_cast<ssize_t>(size_);
    alignas(4) unsigned char reply[4096];
    const ssize_t n = okay ? ::recv(fd, reply, sizeof(reply), 0) : -1;
    nlmsgerr error{};
    okay = n >= static_cast<ssize_t>(NLMSG_LENGTH(sizeof(error)));
    if (okay) std::memcpy(&error, reply + NLMSG_HDRLEN, sizeof(error));
    if (fd >= 0) ::close(fd);
    if (okay && error.error) std::fprintf(stderr, "link request: %s\n", std::strerror(-error.error));
    return okay && error.error == 0;
  }

 private:
  void append(const void* data, std::size_t size) {
    if (data) std::memcpy(bytes_ + size_, data, size);
    size_ += size;
  }
  void align() {
    while (size_ % 4) bytes_[size_++] = 0;
  }
  alignas(4) unsigned char bytes_[1024]{};
  std::size_t size_ = 0;
};

bool createDummy(const char* name, const uint8_t (&mac)[6]) {
  LinkRequest request(RTM_NEWLINK, NLM_F_CREATE | NLM_F_EXCL);
  request.text(IFLA_IFNAME, name);
  request.attribute(IFLA_ADDRESS, mac, sizeof(mac));
  const std::size_t info = request.open(IFLA_LINKINFO);
  request.text(IFLA_INFO_KIND, "dummy");
  request.close(info);
  return request.send();
}

bool createVeth(const char* name, const char* peer, pid_t peerNamespace, const uint8_t (&mac)[6]) {
  LinkRequest request(RTM_NEWLINK, NLM_F_CREATE | NLM_F_EXCL);
  request.text(IFLA_IFNAME, name);
  request.attribute(IFLA_ADDRESS, mac, sizeof(mac));
  const std::size_t info = request.open(IFLA_LINKINFO);
  request.text(IFLA_INFO_KIND, "veth");
  const std::size_t data = request.open(IFLA_INFO_DATA);
  const std::size_t peerInfo = request.open(VETH_INFO_PEER);
  ifinfomsg peerBody{};
  peerBody.ifi_family = AF_UNSPEC;
  request.raw(&peerBody, sizeof(peerBody));
  request.text(IFLA_IFNAME, peer);
  const uint32_t pid = static_cast<uint32_t>(peerNamespace);
  request.attribute(IFLA_NET_NS_PID, &pid, sizeof(pid));
  request.close(peerInfo);
  request.close(data);
  request.close(info);
  return request.send();
}

bool setUp(const char* name) {
  LinkRequest request(RTM_NEWLINK, 0);
  request.body().ifi_index = static_cast<int>(if_nametoindex(name));
  request.body().ifi_flags = IFF_UP | IFF_MULTICAST;
  request.body().ifi_change = IFF_UP | IFF_MULTICAST;
  return request.body().ifi_index > 0 && request.send();
}

bool deleteLink(const char* name) {
  LinkRequest request(RTM_DELLINK, 0);
  request.body().ifi_index = static_cast<int>(if_nametoindex(name));
  return request.body().ifi_index > 0 && request.send();
}

std::vector<KernelAddress> addressesOf(const char* name) {
  Rtnetlink kernel;
  std::vector<KernelAddress> out;
  kernel.addresses(kernel.interfaceIndex(name), out);
  return out;
}

bool hasDefaultVia(const char* name, uint32_t gateway) {
  Rtnetlink kernel;
  std::vector<KernelRoute> routes;
  const int index = kernel.interfaceIndex(name);
  if (!kernel.routes(routes)) return false;
  for (const KernelRoute& route : routes)
    if (route.prefix == 0 && route.ifindex == index && route.gateway == gateway) return true;
  return false;
}

std::size_t defaultRoutes() {
  Rtnetlink kernel;
  std::vector<KernelRoute> routes;
  kernel.routes(routes);
  std::size_t count = 0;
  for (const KernelRoute& route : routes) count += route.prefix == 0;
  return count;
}

std::string nodename() {
  utsname name{};
  uname(&name);
  return name.nodename;
}

unsigned mountsAt(const char* target) { return countMountsAt(readFile("/proc/self/mountinfo"), target); }

void testRtnetlink() {
  const uint8_t mac[6] = {0x02, 0, 0, 0x11, 0x22, 0x33};
  check(createDummy("wlan0", mac) && setUp("wlan0"), "dummy wlan0");
  Rtnetlink kernel;
  const int index = kernel.interfaceIndex("wlan0");
  KernelAddress stock;
  stock.ifindex = index;
  stock.address = ipv4("192.0.2.110");
  stock.prefix = 24;
  stock.broadcast = ipv4("192.0.2.255");
  check(kernel.addAddress(stock) && kernel.replaceDefaultRoute(index, ipv4("192.0.2.1"), false),
        "seed a stock lease: " + std::string(std::strerror(kernel.lastError())));
  std::vector<KernelAddress> addresses = addressesOf("wlan0");
  check(addresses.size() == 1 && addresses[0].address == stock.address && addresses[0].prefix == 24 &&
            addresses[0].broadcast == stock.broadcast,
        "address dump decodes the kernel reply");
  check(hasDefaultVia("wlan0", ipv4("192.0.2.1")), "route dump decodes the kernel reply");

  LeaseApplier applier(kernel, "wlan0");
  std::string error;
  check(applier.flush(error) && addressesOf("wlan0").empty() && defaultRoutes() == 0, "flush on the kernel: " + error);
  DhcpLease lease;
  lease.address = ipv4("10.77.0.23");
  lease.prefix = 24;
  lease.broadcast = ipv4("10.77.0.255");
  lease.router = ipv4("10.77.0.1");
  check(applier.apply(lease, error), "apply on the kernel: " + error);
  check(applier.apply(lease, error), "renew on the kernel: " + error);
  addresses = addressesOf("wlan0");
  check(addresses.size() == 1 && addresses[0].address == lease.address && hasDefaultVia("wlan0", lease.router),
        "kernel holds the lease");
  lease.address = ipv4("10.77.0.24");
  check(applier.apply(lease, error) && addressesOf("wlan0").size() == 1 &&
            addressesOf("wlan0")[0].address == lease.address && hasDefaultVia("wlan0", lease.router),
        "moved lease on the kernel: " + error);
  lease.address = ipv4("10.99.0.5");
  lease.prefix = 32;
  lease.broadcast = 0;
  check(applier.apply(lease, error) && hasDefaultVia("wlan0", lease.router), "on-link gateway for /32: " + error);
  check(applier.remove(error) && addressesOf("wlan0").empty() && defaultRoutes() == 0, "remove on the kernel: " + error);
  check(deleteLink("wlan0"), "dummy removed");
}

void testResolvBind() {
  const std::string target = "/tmp/tc002d-system-resolv.conf";
  const std::string stale = "/tmp/tc002d-system-stale.conf";
  { std::ofstream(target) << "nameserver 1.2.3.4\n"; }
  { std::ofstream(stale) << "nameserver 9.9.9.9\n"; }
  check(::mount(stale.c_str(), target.c_str(), nullptr, MS_BIND, nullptr) == 0, "simulated stale bind");
  {
    BoundResolvConf resolver("/tmp/tc002d-system-own.conf", target);
    std::string error;
    check(resolver.install(error), "install: " + error);
    check(mountsAt(target.c_str()) == 1 && readFile(target).empty(),
          "stale bind replaced, no nameserver taken over from the original");
    check(resolver.write("nameserver 10.77.0.1\n", error) && readFile(target) == "nameserver 10.77.0.1\n",
          "rewrite visible through the bind");
    check(resolver.write("nameserver 10.77.0.1\nnameserver 8.8.8.8\n", error) && resolver.write("nameserver 1.1.1.1\n", error) &&
              readFile(target) == "nameserver 1.1.1.1\n",
          "shorter content truncates");
    resolver.uninstall();
    check(mountsAt(target.c_str()) == 0 && readFile(target) == "nameserver 1.2.3.4\n" &&
              ::access("/tmp/tc002d-system-own.conf", F_OK) != 0,
          "uninstall restores the original");
  }
}

class RecordingPlatform : public IpPlatform {
 public:
  explicit RecordingPlatform(const std::string& runDir) : system_(runDir) {}
  IpKernel& kernel() override { return system_.kernel(); }
  SystemClock& clock() override { return system_.clock(); }
  ResolverFile& resolver() override { return system_.resolver(); }
  pid_t spawn(const SpawnRequest& request, std::string& error) override { return system_.spawn(request, error); }
  void signal(pid_t pid, int number, bool group) override { system_.signal(pid, number, group); }
  bool setKernelHostname(const std::string& hostname) override { return system_.setKernelHostname(hostname); }
  std::string interfaceMac(const std::string& name) override { return system_.interfaceMac(name); }
  bool executable(const std::string& path) override { return system_.executable(path); }
  bool prepareDirectory(const std::string& path) override { return system_.prepareDirectory(path); }
  bool readFile(const std::string& path, std::string& out) override { return system_.readFile(path, out); }
  bool replaceFile(const std::string& path, const std::string& text) override {
    return system_.replaceFile(path, text);
  }
  bool removeFile(const std::string& path) override { return system_.removeFile(path); }
  bool prepareAccessPoint(const std::string& directory, const std::string& config, std::string& error) override {
    return system_.prepareAccessPoint(directory, config, error);
  }
  void removeAccessPoint(const std::string& directory) override { system_.removeAccessPoint(directory); }
  int64_t monotonicMs() override { return system_.monotonicMs(); }
  bool multicast() override { return true; }
  void log(const char* component, const std::string& line) override {
    lines.push_back(std::string(component) + ": " + line);
    system_.log(component, line);
  }
  bool logged(const std::string& text) const {
    for (const std::string& line : lines)
      if (line.find(text) != std::string::npos) return true;
    return false;
  }
  std::vector<std::string> lines;

 private:
  SystemPlatform system_;
};

IpTiming quickTiming() {
  IpTiming timing;
  timing.linkLossGraceMs = 400;
  timing.leaseLossGraceMs = 400;
  timing.clientStopGraceMs = 1000;
  timing.firstRestartMs = 100;
  return timing;
}

SntpTiming quietSntp() {
  SntpTiming timing;
  timing.port = 9;
  return timing;
}

// One iteration of the daemon loop, with an optional extra descriptor for the test itself.
bool drive(IpController& controller, const std::function<bool()>& done, int64_t budgetMs,
           const std::function<void(pid_t, int)>& foreignChild = nullptr, int extraFd = -1,
           const std::function<void()>& onExtra = nullptr) {
  const int64_t end = nowMs() + budgetMs;
  while (nowMs() < end) {
    if (done()) return true;
    std::vector<PollInterest> interest;
    controller.pollInterest(interest);
    std::vector<pollfd> fds;
    for (const PollInterest& entry : interest) fds.push_back({entry.fd, entry.events, 0});
    if (extraFd >= 0) fds.push_back({extraFd, POLLIN, 0});
    int timeout = 20;
    const int64_t deadline = controller.nextDeadlineMs();
    if (deadline >= 0) timeout = static_cast<int>(std::max<int64_t>(0, std::min<int64_t>(20, deadline - nowMs())));
    ::poll(fds.data(), fds.size(), timeout);
    for (const pollfd& entry : fds) {
      if (!entry.revents) continue;
      if (entry.fd == extraFd) onExtra();
      else controller.onReady(entry.fd, entry.revents, nowMs());
    }
    int status = 0;
    for (pid_t child; (child = ::waitpid(-1, &status, WNOHANG)) > 0;) {
      if (!controller.onChildExit(child, status, nowMs()) && foreignChild) foreignChild(child, status);
    }
    const int64_t due = controller.nextDeadlineMs();
    if (due >= 0 && due <= nowMs()) controller.onTime(nowMs());
  }
  return done();
}

void setLink(DeviceState& state, WifiLink link) {
  auto network = state.network();
  network.link = link;
  network.ssid = "System test";
  state.setNetwork(network);
}

std::vector<uint8_t> mdnsQuery(const char* name, uint16_t type, uint16_t id) {
  std::vector<uint8_t> out = {static_cast<uint8_t>(id >> 8), static_cast<uint8_t>(id), 0, 0, 0, 1, 0, 0, 0, 0, 0, 0};
  const std::string text = name;
  std::size_t begin = 0;
  while (begin < text.size()) {
    std::size_t dot = text.find('.', begin);
    if (dot == std::string::npos) dot = text.size();
    out.push_back(static_cast<uint8_t>(dot - begin));
    out.insert(out.end(), text.begin() + static_cast<long>(begin), text.begin() + static_cast<long>(dot));
    begin = dot + 1;
  }
  const uint8_t tail[] = {0, static_cast<uint8_t>(type >> 8), static_cast<uint8_t>(type), 0, 1};
  out.insert(out.end(), std::begin(tail), std::end(tail));
  return out;
}

// Sends one question to the mDNS group from an ephemeral port (a legacy query, answered by
// unicast with the id echoed) and returns the reply, or nothing.
std::vector<uint8_t> mdnsAsk(IpController& controller, uint32_t source, const char* name, uint16_t type,
                             uint16_t id) {
  const int probe = ::socket(AF_INET, SOCK_DGRAM | SOCK_NONBLOCK | SOCK_CLOEXEC, 0);
  in_addr from{};
  from.s_addr = htonl(source);
  const int loop = 1;
  ::setsockopt(probe, IPPROTO_IP, IP_MULTICAST_IF, &from, sizeof(from));
  ::setsockopt(probe, IPPROTO_IP, IP_MULTICAST_LOOP, &loop, sizeof(loop));
  sockaddr_in group{};
  group.sin_family = AF_INET;
  group.sin_port = htons(mdns::kPort);
  group.sin_addr.s_addr = htonl(mdns::kGroup);
  const std::vector<uint8_t> question = mdnsQuery(name, type, id);
  ::sendto(probe, question.data(), question.size(), 0, reinterpret_cast<sockaddr*>(&group), sizeof(group));
  std::vector<uint8_t> reply;
  drive(
      controller, [&] { return !reply.empty(); }, 2000, nullptr, probe, [&] {
        uint8_t bytes[1500];
        const ssize_t n = ::recv(probe, bytes, sizeof(bytes), 0);
        if (n > 12 && bytes[0] == (id >> 8) && bytes[1] == (id & 0xff)) reply.assign(bytes, bytes + n);
      });
  ::close(probe);
  return reply;
}

bool contains(const std::vector<uint8_t>& haystack, const std::vector<uint8_t>& needle) {
  return std::search(haystack.begin(), haystack.end(), needle.begin(), needle.end()) != haystack.end();
}

bool contains(const std::vector<uint8_t>& haystack, const std::string& needle) {
  return contains(haystack, std::vector<uint8_t>(needle.begin(), needle.end()));
}

void testControllerWithFakeClient(const std::string& fake, const std::string& callback) {
  const uint8_t mac[6] = {0x02, 0, 0, 0xaa, 0xbb, 0xcc};
  check(createDummy("wlan0", mac) && setUp("wlan0"), "dummy wlan0 for the controller");
  Rtnetlink seed;
  KernelAddress stock;
  stock.ifindex = seed.interfaceIndex("wlan0");
  stock.address = ipv4("192.0.2.110");
  stock.prefix = 24;
  seed.addAddress(stock);
  seed.replaceDefaultRoute(stock.ifindex, ipv4("192.0.2.1"), false);
  ::unlink("/tmp/fake-udhcpc.argv");

  DeviceState state;
  RecordingPlatform platform("/tmp/tc002d-system-run");
  IpOptions options;
  options.runDir = "/tmp/tc002d-system-run";
  options.udhcpc = fake;
  options.dhcpCallback = callback;
  options.ntpServer = "127.0.0.1";
  options.httpPort = 8080;
  IpController controller(state, options, platform, quickTiming(), quietSntp());
  check(controller.start(nowMs()), "controller start");
  check(nodename() == "awtrixng-aabbcc" && state.network().hostname == "awtrixng-aabbcc", "hostname from sysfs MAC");
  struct stat own {}, visible {};
  check(::stat("/tmp/tc002d-system-run/resolv.conf", &own) == 0 && ::stat("/etc/resolv.conf", &visible) == 0 &&
            own.st_ino == visible.st_ino && mountsAt("/etc/resolv.conf") == 1,
        "our resolv.conf is what /etc/resolv.conf shows");
  check(readFile("/etc/resolv.conf").empty(), "no nameserver before the first lease");
  setLink(state, WifiLink::Connected);
  check(drive(controller, [&] { return state.network().ipv4 == "10.77.0.23"; }, 5000), "lease published");
  const std::vector<KernelAddress> addresses = addressesOf("wlan0");
  check(addresses.size() == 1 && addresses[0].address == ipv4("10.77.0.23") && hasDefaultVia("wlan0", ipv4("10.77.0.1")) &&
            defaultRoutes() == 1,
        "stale stock lease replaced by ours");
  check(readFile("/etc/resolv.conf") == "nameserver 10.77.0.1\nnameserver 10.77.0.2\n", "lease DNS in /etc/resolv.conf");
  check(state.network().gateway == "10.77.0.1" && state.network().dns == "10.77.0.1" &&
            state.network().ssid == "System test",
        "network status");
  check(readFile("/tmp/fake-udhcpc.argv").find("-x hostname:awtrixng-aabbcc") != std::string::npos,
        "hostname passed to udhcpc");
  check(platform.logged("udhcpc: fake client failure") && !platform.logged("broadcasting discover") &&
            !platform.logged("stdout chatter"),
        "client errors reach the log, routine lines and stdout do not");

  const uint32_t self = ipv4("10.77.0.23");
  const std::vector<uint8_t> host = mdnsAsk(controller, self, "awtrixng-aabbcc.local", mdns::kTypeA, 0x4242);
  check(host.size() > 16 && std::equal(host.end() - 4, host.end(), std::vector<uint8_t>{10, 77, 0, 23}.begin()),
        "mDNS answers <hostname>.local over the interface");
  const std::vector<uint8_t> service = mdnsAsk(controller, self, "_awtrixng._tcp.local", mdns::kTypePtr, 0x4243);
  check(contains(service, "id=020000aabbcc") && contains(service, "name=awtrixng-aabbcc") &&
            contains(service, "type=awtrixng") && contains(service, std::vector<uint8_t>{0, 0, 0, 0, 0x1f, 0x90}),
        "_awtrixng._tcp on the HTTP port with the ESP32 TXT entries");
  const std::vector<uint8_t> web = mdnsAsk(controller, self, "_http._tcp.local", mdns::kTypePtr, 0x4244);
  check(contains(web, std::vector<uint8_t>{0, 0, 0, 0, 0x1f, 0x90}), "_http._tcp on the HTTP port");

  controller.setHostname("clock-two");
  check(drive(controller,
              [&] {
                return readFile("/tmp/fake-udhcpc.argv").find("hostname:clock-two") != std::string::npos &&
                       controller.dhcpClient() > 0;
              },
              5000),
        "udhcpc restarted with the new hostname");
  const std::string commands = readFile("/tmp/fake-udhcpc.argv");
  check(nodename() == "clock-two" && commands.find("hostname:clock-two -r 10.77.0.23") != std::string::npos,
        "restart asks for the same address");

  setLink(state, WifiLink::Disconnected);
  check(drive(controller, [&] { return state.network().ipv4.empty() && controller.dhcpClient() < 0; }, 3000),
        "link loss removes the lease after the grace period");
  check(addressesOf("wlan0").empty() && defaultRoutes() == 0, "kernel cleaned after link loss");
  setLink(state, WifiLink::Connected);
  check(drive(controller, [&] { return state.network().ipv4 == "10.77.0.23"; }, 5000), "lease back after link-up");

  const pid_t client = controller.dhcpClient();
  controller.requestStop(nowMs());
  check(drive(controller, [&] { return controller.stopped(); }, 5000), "controller stopped");
  check(::kill(client, 0) != 0 && addressesOf("wlan0").empty() && defaultRoutes() == 0 && state.network().ipv4.empty(),
        "stop removed the lease and the client");
  check(mountsAt("/etc/resolv.conf") == 0, "resolv.conf bind undone");
  check(deleteLink("wlan0"), "dummy removed after the controller test");
}

// Options of the first DHCP client message leaving an interface, captured with a packet socket.
struct ClientMessage {
  bool seen = false;
  std::map<uint8_t, std::string> options;
};

int openDhcpCapture(const char* interfaceName) {
  const int fd = ::socket(AF_PACKET, SOCK_DGRAM | SOCK_NONBLOCK | SOCK_CLOEXEC, htons(ETH_P_ALL));
  sockaddr_ll link{};
  link.sll_family = AF_PACKET;
  link.sll_protocol = htons(ETH_P_ALL);
  link.sll_ifindex = static_cast<int>(if_nametoindex(interfaceName));
  if (fd >= 0 && link.sll_ifindex && ::bind(fd, reinterpret_cast<sockaddr*>(&link), sizeof(link)) == 0) return fd;
  if (fd >= 0) ::close(fd);
  return -1;
}

void readDhcpCapture(int fd, ClientMessage& out) {
  uint8_t packet[1600];
  sockaddr_ll from{};
  socklen_t fromSize = sizeof(from);
  for (ssize_t n; (n = ::recvfrom(fd, packet, sizeof(packet), 0, reinterpret_cast<sockaddr*>(&from), &fromSize)) > 0;
       fromSize = sizeof(from)) {
    const std::size_t size = static_cast<std::size_t>(n), header = (packet[0] & 0x0fu) * 4u;
    if (out.seen || from.sll_protocol != htons(ETH_P_IP) || size < header + 8 + 240 || packet[9] != IPPROTO_UDP)
      continue;
    const uint8_t* udp = packet + header;
    const uint8_t* bootp = udp + 8;
    if (((udp[2] << 8) | udp[3]) != 67 || bootp[0] != 1 || std::memcmp(bootp + 236, "\x63\x82\x53\x63", 4) != 0)
      continue;
    const uint8_t* end = packet + size;
    for (const uint8_t* at = bootp + 240; at + 2 <= end && *at != 255;) {
      if (*at == 0) {
        ++at;
        continue;
      }
      if (at + 2 + at[1] > end) break;
      out.options[at[0]] = std::string(reinterpret_cast<const char*>(at + 2), at[1]);
      at += 2 + at[1];
    }
    out.seen = true;
  }
}

void testRealUdhcpc(const std::string& busybox, const std::string& callback) {
  int ready[2], go[2];
  if (::pipe(ready) != 0 || ::pipe(go) != 0) return check(false, "namespace pipes");
  const pid_t server = ::fork();
  if (server == 0) {
    ::close(ready[0]);
    ::close(go[1]);
    if (::unshare(CLONE_NEWNET) != 0) _exit(10);
    char byte = 1;
    if (::write(ready[1], &byte, 1) != 1 || ::read(go[0], &byte, 1) != 1) _exit(11);
    if (!setUp("srv0") || !setUp("lo")) _exit(12);
    Rtnetlink kernel;
    KernelAddress address;
    address.ifindex = kernel.interfaceIndex("srv0");
    address.address = ipv4("10.88.0.1");
    address.prefix = 24;
    address.broadcast = ipv4("10.88.0.255");
    if (!kernel.addAddress(address)) _exit(13);
    std::ofstream("/tmp/tc002d-udhcpd.conf")
        << "interface srv0\nstart 10.88.0.100\nend 10.88.0.150\nlease_file /tmp/tc002d-udhcpd.leases\n"
        << "option subnet 255.255.255.0\noption router 10.88.0.1\noption dns 10.88.0.53\noption ntpsrv 10.88.0.1\n"
        << "option lease 120\n";
    ::close(creat("/tmp/tc002d-udhcpd.leases", 0644));
    execl(busybox.c_str(), "udhcpd", "-f", "/tmp/tc002d-udhcpd.conf", static_cast<char*>(nullptr));
    _exit(14);
  }
  ::close(ready[1]);
  ::close(go[0]);
  char byte = 0;
  const uint8_t mac[6] = {0x02, 0, 0, 0x12, 0x34, 0x56};
  const bool linked = ::read(ready[0], &byte, 1) == 1 && createVeth("wlan0", "srv0", server, mac) && setUp("wlan0");
  check(linked, "veth pair into the server namespace");
  byte = 1;
  if (::write(go[1], &byte, 1) != 1) check(false, "server start");
  ::close(ready[0]);
  ::close(go[1]);

  DeviceState state;
  RecordingPlatform platform("/tmp/tc002d-system-real");
  IpOptions options;
  options.runDir = "/tmp/tc002d-system-real";
  options.udhcpc = busybox;
  options.dhcpCallback = callback;
  options.ntpServer = "127.0.0.1";
  IpController controller(state, options, platform, quickTiming(), quietSntp());
  bool serverGone = false;
  const auto foreign = [&](pid_t pid, int) { serverGone = serverGone || pid == server; };
  const int capture = openDhcpCapture("wlan0");
  check(capture >= 0, "packet capture on wlan0");
  ClientMessage client;
  check(controller.start(nowMs()), "controller start for real udhcpc");
  setLink(state, WifiLink::Connected);
  check(drive(controller, [&] { return state.network().ipv4.rfind("10.88.0.", 0) == 0; }, 20000, foreign, capture,
              [&] { readDhcpCapture(capture, client); }),
        "real udhcpc obtained a lease from udhcpd");
  if (capture >= 0) ::close(capture);
  check(client.seen && client.options.count(12) && client.options[12] == "awtrixng-123456",
        "hostname option carries our hostname");
  check(client.seen && !client.options.count(60), "no vendor class on the wire");
  check(client.options.count(61) && client.options[61] == std::string("\x01\x02\x00\x00\x12\x34\x56", 7),
        "client id is the MAC and nothing else");
  const std::string address = state.network().ipv4;
  check(hasDefaultVia("wlan0", ipv4("10.88.0.1")) && readFile("/etc/resolv.conf") == "nameserver 10.88.0.53\n" &&
            state.network().gateway == "10.88.0.1",
        "real lease applied: route and DNS");
  const std::string commandLine = readFile("/proc/" + std::to_string(controller.dhcpClient()) + "/cmdline");
  check(commandLine.find(std::string("hostname:awtrixng-123456")) != std::string::npos, "udhcpc sends the hostname");
  check(commandLine.find(std::string("-V\0\0", 4)) != std::string::npos, "udhcpc runs with an empty vendor class");

  setLink(state, WifiLink::Disconnected);
  drive(controller, [] { return false; }, 100, foreign);
  setLink(state, WifiLink::Connected);
  check(drive(controller, [&] { return platform.logged("renew " + address); }, 10000, foreign),
        "short link loss renews the lease over unicast");
  check(state.network().ipv4 == address, "renewal kept the address");
  check(!platform.logged("udhcpc: broadcasting") && !platform.logged("udhcpc: lease of") &&
            !platform.logged("udhcpc: sending renew"),
        "routine udhcpc output stays out of the log");

  controller.requestStop(nowMs());
  check(drive(controller, [&] { return controller.stopped(); }, 5000, foreign), "real client stopped");
  check(addressesOf("wlan0").empty() && defaultRoutes() == 0, "real lease removed on stop");
  ::kill(server, SIGTERM);
  int status = 0;
  if (!serverGone) ::waitpid(server, &status, 0);
  deleteLink("wlan0");
}

int accessPointPeer(const std::string& busybox, const std::string& callback) {
  if (!setUp("peer0") || !setUp("lo")) return 20;
  int events[2];
  if (::pipe2(events, O_CLOEXEC | O_NONBLOCK) != 0) return 21;
  SpawnRequest request;
  request.path = busybox;
  request.argv = {"udhcpc", "-f", "-q", "-n", "-i", "peer0", "-s", callback, "-t", "4", "-T", "1", "-V", ""};
  request.environment = {"PATH=/usr/sbin:/usr/bin:/sbin:/bin"};
  request.passFd = events[1];
  request.passAs = kDhcpEventFd;
  std::string error;
  const pid_t child = spawnChild(request, error);
  ::close(events[1]);
  if (child <= 0) { ::close(events[0]); return 22; }
  std::string records;
  DhcpRecord lease;
  bool bound = false;
  const int64_t deadline = nowMs() + 10000;
  while (!bound && nowMs() < deadline && records.size() < 8192) {
    pollfd event{events[0], POLLIN, 0};
    ::poll(&event, 1, 100);
    char buffer[2048];
    const ssize_t n = ::read(events[0], buffer, sizeof(buffer));
    if (n > 0) records.append(buffer, static_cast<std::size_t>(n));
    else if (n == 0) break;
    for (std::size_t end; (end = records.find('\n')) != std::string::npos;) {
      const std::string line = records.substr(0, end);
      records.erase(0, end + 1);
      if (parseDhcpRecord(line, "peer0", lease, error) && lease.event == DhcpEvent::Bound) {
        bound = true;
        break;
      }
    }
  }
  ::close(events[0]);
  ::kill(child, SIGKILL);
  int status = 0;
  ::waitpid(child, &status, 0);
  if (!bound || lease.lease.prefix != 24 || lease.lease.router != kAccessPointAddress ||
      lease.lease.dns != std::vector<uint32_t>({kAccessPointAddress}) ||
      lease.lease.address < kAccessPointAddress + 1 || lease.lease.address > kAccessPointAddress + 8) return 23;
  Rtnetlink kernel;
  LeaseApplier applier(kernel, "peer0");
  if (!applier.apply(lease.lease, error)) return 24;
  const int probe = ::socket(AF_INET, SOCK_DGRAM | SOCK_CLOEXEC, 0);
  if (probe < 0) return 25;
  const timeval timeout{3, 0};
  ::setsockopt(probe, SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof(timeout));
  sockaddr_in server{};
  server.sin_family = AF_INET;
  server.sin_addr.s_addr = htonl(kAccessPointAddress);
  server.sin_port = htons(53);
  const auto query = mdnsQuery("setup.invalid", 1, 0x7192);
  ::sendto(probe, query.data(), query.size(), 0, reinterpret_cast<const sockaddr*>(&server), sizeof(server));
  uint8_t response[512];
  const ssize_t n = ::recv(probe, response, sizeof(response), 0);
  ::close(probe);
  return n > 0 && std::vector<uint8_t>(response, response + n) == captiveDnsReply(query.data(), query.size()) ? 0 : 26;
}

void testRealAccessPoint(const std::string& busybox, const std::string& callback) {
  int ready[2], go[2];
  if (::pipe(ready) != 0 || ::pipe(go) != 0) return check(false, "AP namespace pipes");
  const pid_t peer = ::fork();
  if (peer == 0) {
    ::close(ready[0]);
    ::close(go[1]);
    if (::unshare(CLONE_NEWNET) != 0) _exit(10);
    char byte = 1;
    if (::write(ready[1], &byte, 1) != 1 || ::read(go[0], &byte, 1) != 1) _exit(11);
    ::close(ready[1]);
    ::close(go[0]);
    _exit(accessPointPeer(busybox, callback));
  }
  ::close(ready[1]);
  ::close(go[0]);
  char byte = 0;
  const uint8_t mac[6] = {0x02, 0, 0, 0x65, 0x43, 0x21};
  const bool linked = peer > 0 && ::read(ready[0], &byte, 1) == 1 &&
                      createVeth("wlan0", "peer0", peer, mac) && setUp("wlan0");
  check(linked, "AP veth pair into the client namespace");
  DeviceState state;
  const std::string directory = "/tmp/tc002d-system-ap";
  RecordingPlatform platform(directory);
  IpOptions options;
  options.runDir = directory;
  options.udhcpc = busybox;
  options.dhcpCallback = callback;
  options.ntpServer = "127.0.0.1";
  IpController controller(state, options, platform, quickTiming(), quietSntp());
  check(controller.start(nowMs()), "AP controller starts with production system platform");
  setLink(state, WifiLink::AccessPoint);
  controller.onTime(nowMs());
  check(state.network().ipv4 == "192.168.4.1", "AP static address applied on the kernel");
  byte = 1;
  check(::write(go[1], &byte, 1) == 1, "AP client starts");
  ::close(ready[0]);
  ::close(go[1]);
  bool peerDone = false;
  int peerStatus = -1;
  const auto exited = [&](pid_t pid, int status) {
    if (pid == peer) { peerDone = true; peerStatus = status; }
  };
  drive(controller, [&] { return peerDone; }, 15000, exited);
  check(peerDone && WIFEXITED(peerStatus) && WEXITSTATUS(peerStatus) == 0,
        "real AP client receives bounded DHCP lease/router/DNS and captive A response, status " + std::to_string(peerStatus));
  if (!peerDone && peer > 0) { ::kill(peer, SIGKILL); ::waitpid(peer, nullptr, 0); }
  controller.requestStop(nowMs());
  check(drive(controller, [&] { return controller.stopped(); }, 5000), "real AP server is reaped on shutdown");
  check(addressesOf("wlan0").empty() && defaultRoutes() == 0 &&
            ::access((directory + "/access-point/leases").c_str(), F_OK) != 0,
        "AP shutdown removes address, DHCP lease file and resolver bind");
  check(mountsAt("/etc/resolv.conf") == 0, "AP resolver bind undone");
  deleteLink("wlan0");
}

}

int main(int argc, char** argv) {
  if (argc < 3) {
    std::fprintf(stderr, "usage: tc002d-ip-system FAKE_UDHCPC CALLBACK [HOST_BUSYBOX_WITH_UDHCPD]\n");
    return 2;
  }
  if (::geteuid() != 0 || ::unshare(CLONE_NEWNS) != 0 ||
      ::mount(nullptr, "/", nullptr, MS_REC | MS_PRIVATE, nullptr) != 0) {
    std::printf("SKIP: needs root with CAP_SYS_ADMIN and CAP_NET_ADMIN in a throwaway network namespace\n");
    return 77;
  }
  ::signal(SIGPIPE, SIG_IGN);
  testRtnetlink();
  testResolvBind();
  testControllerWithFakeClient(argv[1], argv[2]);
  if (argc > 3) {
    testRealUdhcpc(argv[3], argv[2]);
    testRealAccessPoint(argv[3], argv[2]);
  }
  return ip_test::finish("tc002d-ip-system");
}
