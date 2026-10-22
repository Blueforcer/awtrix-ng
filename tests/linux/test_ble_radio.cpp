#include "../support.h"
// Exercise the Linux Bluetooth socket backend without a Bluetooth adapter. The linker wrappers
// substitute Unix sockets at the system boundary; the radio still uses its real public API.
#include <sys/socket.h>
#include <sys/utsname.h>
#include <unistd.h>
#include <fcntl.h>

#include <algorithm>
#include <cerrno>
#include <chrono>
#include <cstdio>
#include <cstring>
#include <deque>
#include <map>
#include <memory>
#include <string>
#include <thread>
#include <vector>

#include "platform/linux/ble/BleService.h"
#include "platform/linux/ble/LinuxBleRadio.h"

using awtrix::ble::Address;
using awtrix::ble::BleRadio;
using awtrix::ble::BleService;
using awtrix::ble::Bytes;
using awtrix::ble::LinuxBleRadio;
using awtrix::ble::Power;

extern "C" {
int __real_socket(int, int, int);
int __real_bind(int, const sockaddr*, socklen_t);
int __real_listen(int, int);
int __real_setsockopt(int, int, int, const void*, socklen_t);
int __real_getsockopt(int, int, int, void*, socklen_t*);
int __real_connect(int, const sockaddr*, socklen_t);
int __real_accept4(int, sockaddr*, socklen_t*, int);
ssize_t __real_write(int, const void*, size_t);
int __real_uname(utsname*);
}

namespace {

constexpr int kBluetooth = 31;
constexpr int kHci = 1;
constexpr int kL2cap = 0;
constexpr int kBluetoothLevel = 274;
constexpr int kSecurity = 4;
constexpr uint16_t kScanParameters = 0x200b;
constexpr uint16_t kScanEnable = 0x200c;

// The public Linux sockaddr layouts, also usable on build hosts without Bluetooth headers.
struct HciAddress {
  sa_family_t family;
  unsigned short device;
  unsigned short channel;
};
struct L2Address {
  sa_family_t family;
  unsigned short psm;
  uint8_t address[6];
  unsigned short cid;
  uint8_t type;
};

int& failures = awtrix::test::failures();
using awtrix::test::check;

uint16_t word(const uint8_t* p) { return static_cast<uint16_t>(p[0] | (p[1] << 8)); }
void appendWord(Bytes& out, uint16_t n) {
  out.push_back(static_cast<uint8_t>(n));
  out.push_back(static_cast<uint8_t>(n >> 8));
}

enum class Kind { Unbound, Management, Scanner, Listener, Link };
struct Endpoint {
  int peer = -1;
  int protocol = 0;
  Kind kind = Kind::Unbound;
  uint16_t connectionHandle = 0;
};
struct Incoming {
  int fd;
  Address address;
};

struct SocketController {
  std::map<int, Endpoint> endpoints;
  std::deque<Incoming> incoming;
  std::vector<Bytes> commands;
  std::vector<Bytes> rawCommands;
  std::vector<Bytes> managementCommands;
  uint8_t secureConnectionsStatus = 0;
  uint8_t privacyStatus = 0;
  std::vector<std::pair<int, int>> securityRequests;
  std::string kernelRelease = "4.9.84";
  bool controllerAttached = true;
  int scanner = -1;
  int listener = -1;
  int outgoing = -1;
  int connectError = 0;
  int connectCalls = 0;
  // A raw HCI command whose write fails, as on a controller that went away.
  uint16_t unwritable = 0;

  ~SocketController() {
    for (const auto& [fd, endpoint] : endpoints) {
      (void)fd;
      if (endpoint.peer >= 0) ::close(endpoint.peer);
    }
  }

  int socket(int protocol) {
    int pair[2];
    if (::socketpair(AF_UNIX, SOCK_SEQPACKET | SOCK_CLOEXEC | SOCK_NONBLOCK, 0, pair) < 0) return -1;
    endpoints[pair[0]] = {pair[1], protocol, Kind::Unbound};
    return pair[0];
  }

  void sendTo(int fd, const Bytes& bytes) {
    const auto it = endpoints.find(fd);
    check(it != endpoints.end() && it->second.peer >= 0, "fake socket is available");
    if (it != endpoints.end() && it->second.peer >= 0)
      check(__real_write(it->second.peer, bytes.data(), bytes.size()) == static_cast<ssize_t>(bytes.size()),
            "fake socket receives its packet");
  }

  void answerManagement(int fd, const uint8_t* data) {
    const uint16_t op = word(data);
    managementCommands.emplace_back(data, data + 6 + word(data + 4));
    Bytes result;
    if (op == 0x0003) result = controllerAttached ? Bytes{1, 0, 0, 0} : Bytes{0, 0};
    else if (op == 0x0004) result = {1, 0, 0, 0, 0, 2};
    else if (op == 0x003d) result = {0, 0, 0, 0, 0, 0, 2};  // Two advertising slots.
    Bytes reply;
    appendWord(reply, 0x0001);  // Management Command Complete.
    appendWord(reply, word(data + 2));
    appendWord(reply, static_cast<uint16_t>(3 + result.size()));
    appendWord(reply, op);
    reply.push_back(op == 0x002d ? secureConnectionsStatus : op == 0x002f ? privacyStatus : 0);
    reply.insert(reply.end(), result.begin(), result.end());
    sendTo(fd, reply);
  }

  int queueIncoming(const Address& address) {
    const int fd = socket(kL2cap);
    if (fd < 0) return fd;
    endpoints[fd].kind = Kind::Link;
    incoming.push_back({fd, address});
    sendTo(listener, {1});
    return fd;
  }

  void closePeer(int fd) {
    auto& endpoint = endpoints.at(fd);
    ::close(endpoint.peer);
    endpoint.peer = -1;
  }

  bool scanRestarted(bool active = true, bool background = true) const {
    if (commands.size() != 3) return false;
    return commands[0] == Bytes({1, 0x0c, 0x20, 2, 0, 0}) &&
           commands[1] == Bytes({1, 0x0b, 0x20, 7, static_cast<uint8_t>(active),
                                 static_cast<uint8_t>(background ? 0 : 0xa0),
                                 static_cast<uint8_t>(background ? 4 : 0), 0x30, 0, 0, 0}) &&
           commands[2] == Bytes({1, 0x0c, 0x20, 2, 1, 0});
  }

  std::size_t countCommand(uint16_t opcode) const {
    return static_cast<std::size_t>(std::count_if(rawCommands.begin(), rawCommands.end(),
                                                [&](const Bytes& packet) { return word(packet.data() + 1) == opcode; }));
  }
};

SocketController* controller = nullptr;

struct Fixture {
  SocketController sockets;
  std::vector<std::string> logs;
  std::vector<int> accepted;
  std::vector<int> disconnected;
  std::vector<int> connected;
  std::vector<int> failedConnections;
  std::unique_ptr<LinuxBleRadio> radio;
  bool powered = false;
  int64_t now = 100;

  explicit Fixture(int attempts = 3, std::string release = "4.9.84", uint8_t scStatus = 0,
                   bool logParameters = false, bool privacy = false, uint8_t privacyStatus = 0) {
    controller = &sockets;
    sockets.kernelRelease = std::move(release);
    sockets.secureConnectionsStatus = scStatus;
    sockets.privacyStatus = privacyStatus;
    LinuxBleRadio::Options options;
    options.connectAttempts = attempts;
    options.logConnectionParameters = logParameters;
    options.privacy = privacy;
    options.log = [this](const std::string& line) { logs.push_back(line); };
    radio = std::make_unique<LinuxBleRadio>(std::move(options));
    BleRadio::Events events;
    events.power = [this](Power state, const std::string&) { powered = state == Power::On; };
    events.accepted = [this](int id, const Address&) { accepted.push_back(id); };
    events.disconnected = [this](int id) { disconnected.push_back(id); };
    events.connected = [this](int id, bool ok, const std::string&) {
      (ok ? connected : failedConnections).push_back(id);
    };
    radio->setEvents(std::move(events));
    radio->power(true);
    if (privacy && privacyStatus != 0)
      check(!powered, "a rejected pairing configuration does not power on");
    else check(powered && sockets.scanner >= 0 && sockets.listener >= 0, "radio powers through the socket backend");
  }

  ~Fixture() {
    radio.reset();
    controller = nullptr;
  }

  void dispatch(int fd, short event) {
    now += 10;
    radio->dispatch({pollfd{fd, event, event}}, now);
  }

  int accept(const char* address = "AA:BB:CC:DD:EE:01", bool random = false) {
    Address peer;
    Address::parse(address, random, peer);
    const int fd = sockets.queueIncoming(peer);
    dispatch(sockets.listener, POLLIN);
    check(!accepted.empty(), "incoming phone reaches the public accepted event");
    return fd;
  }

  void commandComplete(uint16_t opcode, uint8_t status = 0) {
    Bytes packet{4, 0x0e, 4, 1};
    appendWord(packet, opcode);
    packet.push_back(status);
    sockets.sendTo(sockets.scanner, packet);
    dispatch(sockets.scanner, POLLIN);
  }

  void leConnectionComplete(const Address& peer, uint8_t status, uint16_t handle = 0x42, bool enhanced = false,
                            const Address* peerRpa = nullptr) {
    Bytes packet{4, 0x3e, static_cast<uint8_t>(enhanced ? 31 : 19),
                 static_cast<uint8_t>(enhanced ? 0x0a : 1), status};
    appendWord(packet, handle);
    packet.push_back(0);  // Local master role.
    packet.push_back(peer.random ? 1 : 0);
    packet.insert(packet.end(), peer.b.begin(), peer.b.end());
    packet.resize(enhanced ? 34 : 22);
    if (enhanced && peerRpa) std::copy(peerRpa->b.begin(), peerRpa->b.end(), packet.begin() + 21);
    sockets.sendTo(sockets.scanner, packet);
    dispatch(sockets.scanner, POLLIN);
  }

  void disconnectionComplete(uint16_t handle) {
    Bytes packet{4, 5, 4, 0};
    appendWord(packet, handle);
    packet.push_back(0x13);
    sockets.sendTo(sockets.scanner, packet);
    dispatch(sockets.scanner, POLLIN);
  }
};

void connectionParameterDiagnostics() {
  Fixture fixture(3, "4.9.84", 0, true);
  const auto send = [&](const Bytes& packet) {
    fixture.sockets.sendTo(fixture.sockets.scanner, packet);
    fixture.dispatch(fixture.sockets.scanner, POLLIN);
  };
  const auto logged = [&](const std::string& value) {
    return std::any_of(fixture.logs.begin(), fixture.logs.end(), [&](const std::string& line) {
      return line.find(value) != std::string::npos;
    });
  };
  const auto commands = fixture.sockets.rawCommands.size();
  for (bool enhanced : {false, true}) {
    Bytes packet{4, 0x3e, static_cast<uint8_t>(enhanced ? 31 : 19),
                 static_cast<uint8_t>(enhanced ? 0x0a : 1), 0, 18, 0,
                 static_cast<uint8_t>(enhanced ? 1 : 0), 0};
    packet.resize(enhanced ? 27 : 15, 0);
    appendWord(packet, 7);
    appendWord(packet, 2);
    appendWord(packet, 300);
    packet.push_back(0);
    send(packet);
  }
  check(logged("central, interval 8.75ms, latency 2, timeout 3000ms") &&
        logged("peripheral, interval 8.75ms, latency 2, timeout 3000ms"),
        "ordinary and enhanced connection events decode role, offsets and units");
  send({4, 0x3e, 10, 3, 0, 18, 0, 40, 0, 0, 0, 60, 0});
  check(logged("updated: interval 50.00ms, latency 0, timeout 600ms"),
        "successful update reports the applied parameters");
  send({4, 0x3e, 10, 3, 0x1e, 18, 0, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff});
  check(logged("update failed (status 0x1e)") && !logged("81918.75"),
        "failed update does not report undefined parameter fields");
  send({4, 0x3e, 11, 6, 18, 0, 7, 0, 9, 0, 0, 0, 44, 1});
  check(logged("peer request: interval 8.75..11.25ms, latency 0, timeout 3000ms"),
        "remote requests report the requested interval range");
  const auto count = fixture.logs.size();
  send({4, 0x3e, 10, 3, 0, 18, 0});
  send({4, 0x3e, 4, 6, 18, 0, 0});
  send({4, 0x3e, 1, 0x0a});
  check(fixture.logs.size() == count, "truncated parameter events are ignored");
  check(fixture.sockets.rawCommands.size() == commands, "parameter diagnostics send no HCI commands");
  for (const auto& line : fixture.logs)
    check(line.size() < 120, "connection parameter log lines fit the clock log");
}

void steadyTimingUsesTheSocketHandle() {
  for (bool incoming : {false, true}) {
    Fixture fixture(3, "4.9.84", 0, true);
    Address peer;
    Address::parse("68:6C:E6:7A:21:A5", false, peer);
    int id, fd;
    if (incoming) {
      fd = fixture.accept("68:6C:E6:7A:21:A5");
      id = fixture.accepted.back();
    } else {
      id = fixture.radio->connect(peer, 2);
      fd = fixture.sockets.outgoing;
      check(!fixture.radio->requestSteadyTiming(id), "pending connections cannot request timing");
      fixture.dispatch(fd, POLLOUT);
    }
    fixture.sockets.endpoints.at(fd).connectionHandle = 18;
    check(!fixture.radio->requestSteadyTiming(id), "unknown HCI role cannot request timing");
    fixture.leConnectionComplete(peer, 0, 18, incoming);
    check(fixture.sockets.countCommand(0x2013) == 0, "connection event alone leaves timing unchanged");
    fixture.sockets.unwritable = 0x2013;
    check(!fixture.radio->requestSteadyTiming(id), "a failed HCI write is not reported as queued");
    fixture.sockets.unwritable = 0;
    check(fixture.radio->requestSteadyTiming(id), "both socket paths can request central timing");
    const Bytes expected{1, 0x13, 0x20, 14, 18, 0, 40, 0, 40, 0, 0, 0, 60, 0, 0, 0, 0, 0};
    check(fixture.sockets.rawCommands.back() == expected,
          "LE Connection Update targets the socket handle with 50ms, zero latency and 600ms timeout");
    fixture.sockets.sendTo(fixture.sockets.scanner, {4, 0x0f, 4, 0x0c, 1, 0x13, 0x20});
    fixture.dispatch(fixture.sockets.scanner, POLLIN);
    check(fixture.logs.back().find("HCI 0x2013 refused (status 0x0c)") != std::string::npos,
          "controller refusal of the timing request is logged");
    fixture.sockets.sendTo(fixture.sockets.scanner, {4, 0x0f, 4, 0, 1, 0x13, 0x20});
    fixture.dispatch(fixture.sockets.scanner, POLLIN);
    check(fixture.logs.back().find("accepted; waiting for connection update result") != std::string::npos,
          "command acceptance does not claim negotiated timing");
    fixture.disconnectionComplete(18);
    check(!fixture.radio->requestSteadyTiming(id), "disconnection clears the handle role");
    Bytes peripheral{4, 0x3e, 19, 1, 0, 18, 0, 1, 0};
    peripheral.resize(22, 0);
    fixture.sockets.sendTo(fixture.sockets.scanner, peripheral);
    fixture.dispatch(fixture.sockets.scanner, POLLIN);
    const auto count = fixture.sockets.countCommand(0x2013);
    check(!fixture.radio->requestSteadyTiming(id) && fixture.sockets.countCommand(0x2013) == count,
          "a handle reused for a peripheral connection is never updated");
    fixture.radio->disconnect(id);
    check(!fixture.radio->requestSteadyTiming(id), "closed links cannot request timing");
  }
}

void pairingMethodMetadataContainsNoKeyMaterial() {
  Fixture fixture;
  int management = -1;
  for (const auto& [fd, endpoint] : fixture.sockets.endpoints)
    if (endpoint.kind == Kind::Management) management = fd;
  check(management >= 0, "pairing metadata test has a management socket");
  for (const auto type : {uint8_t{0}, uint8_t{2}, uint8_t{255}}) {
    Bytes event;
    appendWord(event, 0x000a);
    appendWord(event, 0);
    appendWord(event, 37);
    Bytes parameters(37, 0xa5);
    parameters[0] = 0;
    parameters[7] = 1;
    parameters[8] = type;
    parameters[10] = 16;
    event.insert(event.end(), parameters.begin(), parameters.end());
    fixture.sockets.sendTo(management, event);
    fixture.dispatch(management, POLLIN);
    const auto& line = fixture.logs.back();
    const std::string expected = type == 0 ? "legacy (unauthenticated)" :
                                 type == 2 ? "LE Secure Connections (unauthenticated)" : "unknown type 255";
    check(line.find(expected) != std::string::npos && line.find("key size 16 bytes") != std::string::npos,
          "new pairing metadata distinguishes legacy, Secure Connections and unknown key types");
    check(line.find("a5a5") == std::string::npos && line.find("A5A5") == std::string::npos,
          "pairing diagnostics do not expose key bytes");
  }
}

void localBleIdentityConfiguration() {
  for (uint8_t status : {uint8_t{0}, uint8_t{0x0c}}) {
    Fixture fixture(3, "4.9.84", 0, false, true, status);
    const auto& commands = fixture.sockets.managementCommands;
    const auto privacy = std::find_if(commands.begin(), commands.end(), [](const Bytes& command) {
      return word(command.data()) == 0x002f;
    });
    const auto power = std::find_if(commands.begin(), commands.end(), [](const Bytes& command) {
      return word(command.data()) == 0x0005;
    });
    check(privacy != commands.end() && privacy->size() == 23 && (*privacy)[6] == 1,
          "Set Privacy carries normal privacy mode and a full local IRK");
    check(status == 0 ? power != commands.end() && privacy < power : power == commands.end(),
          "local BLE identity is configured before power and rejection does not silently continue");
    if (status == 0) {
      const Bytes original = *privacy;
      const std::string encoded = awtrix::ble::toHex(Bytes(original.begin() + 7, original.end()));
      fixture.radio->power(false);
      fixture.radio->power(true);
      const auto last = std::find_if(commands.rbegin(), commands.rend(), [](const Bytes& command) {
        return word(command.data()) == 0x002f;
      });
      check(last != commands.rend() && *last == original, "radio power cycles preserve its local identity");
      check(std::none_of(fixture.logs.begin(), fixture.logs.end(), [&](const std::string& line) {
        return line.find(encoded) != std::string::npos;
      }), "local identity bytes are never logged");
    }
  }
  Fixture defaults;
  check(std::none_of(defaults.sockets.managementCommands.begin(), defaults.sockets.managementCommands.end(),
                     [](const Bytes& command) { return word(command.data()) == 0x002f; }),
        "privacy is opt-in for radio users outside the gamepad test configuration");
}

void secureConnectionsConfiguration() {
  for (uint8_t status : {uint8_t{0}, uint8_t{0x0c}}) {
    Fixture fixture(3, "4.9.84", status);
    const auto& commands = fixture.sockets.managementCommands;
    auto secure = std::find_if(commands.begin(), commands.end(), [](const Bytes& command) {
      return word(command.data()) == 0x002d;
    });
    auto power = std::find_if(commands.begin(), commands.end(), [](const Bytes& command) {
      return word(command.data()) == 0x0005;
    });
    check(secure != commands.end() && power != commands.end() && secure < power &&
              secure->size() == 7 && (*secure)[6] == 1,
          "Secure Connections is enabled before power, without requiring SC-only mode");
    const std::string expected = status == 0 ? "LE Secure Connections enabled" :
                                             "LE Secure Connections unavailable (status 12)";
    check(std::any_of(fixture.logs.begin(), fixture.logs.end(), [&](const std::string& line) {
      return line.find(expected) != std::string::npos;
    }), "Secure Connections result is visible, and unsupported controllers still power on");
  }
}

void incomingRestoresScan() {
  Fixture fixture;
  check(fixture.radio->scan(true, true, true), "active background scan starts");
  fixture.sockets.commands.clear();
  const int fd = fixture.accept();
  check(fixture.sockets.scanRestarted(), "incoming phone restores the requested active background scan");

  fixture.sockets.commands.clear();
  fixture.sockets.closePeer(fd);
  fixture.dispatch(fd, POLLHUP);
  check(fixture.disconnected == fixture.accepted, "incoming disconnect reaches the public event");
  check(fixture.sockets.scanRestarted(), "incoming disconnect restores the requested scan");

  fixture.accept();
  fixture.sockets.commands.clear();
  fixture.radio->disconnect(fixture.accepted.back());
  check(fixture.sockets.scanRestarted(), "locally closing an incoming connection restores the scan");
}

void pendingConnectOwnsScan() {
  Fixture fixture;
  fixture.radio->scan(true, false, true);
  Address pad;
  Address::parse("AA:BB:CC:DD:EE:02", false, pad);
  check(fixture.radio->connect(pad, 1) >= 0, "outgoing gamepad starts connecting");
  fixture.sockets.commands.clear();
  const int fd = fixture.accept();
  fixture.sockets.closePeer(fd);
  fixture.dispatch(fd, POLLHUP);
  fixture.radio->scan(true, true, false);
  fixture.radio->tick(20000);
  check(fixture.sockets.commands.empty(), "incoming events and quiet recovery leave a pending kernel connect scan alone");
  fixture.dispatch(fixture.sockets.outgoing, POLLOUT);
  check(fixture.sockets.scanRestarted(true, false), "completed outgoing connection restores the latest scan request");
}

void disabledScanStaysOff() {
  Fixture fixture;
  fixture.radio->scan(true, true, true);
  fixture.radio->scan(false, true, true);
  fixture.sockets.commands.clear();
  const int fd = fixture.accept();
  fixture.sockets.closePeer(fd);
  fixture.dispatch(fd, POLLHUP);
  check(fixture.sockets.commands.empty(), "incoming events do not restart a disabled scan");
}

void directConnectionPreservesPhone() {
  Fixture fixture;
  const int phone = fixture.accept();
  fixture.radio->scan(true, true, true);
  fixture.sockets.commands.clear();
  fixture.sockets.rawCommands.clear();
  Address pad;
  Address::parse("AA:BB:CC:DD:EE:02", true, pad);
  const int id = fixture.radio->connect(pad, 2);
  check(id >= 0, "legacy controller allocates a pending gamepad link");
  check(fixture.sockets.connectCalls == 0 && fixture.sockets.countCommand(0x200d) == 1,
        "legacy incoming phone uses raw LE Create Connection instead of the blocked L2CAP connect");
  const auto command = std::find_if(fixture.sockets.rawCommands.begin(), fixture.sockets.rawCommands.end(),
                                   [](const Bytes& packet) { return word(packet.data() + 1) == 0x200d; });
  check(command != fixture.sockets.rawCommands.end() && command->size() == 29 && (*command)[9] == 1 &&
            std::equal(pad.b.begin(), pad.b.end(), command->begin() + 10),
        "raw connection uses the gamepad's random address and the full command payload");

  fixture.sockets.commands.clear();
  fixture.accept("AA:BB:CC:DD:EE:03");
  check(fixture.connected.empty() && fixture.accepted.size() == 2,
        "an unrelated incoming peer does not complete the pending gamepad");
  check(fixture.sockets.commands.empty(), "an unrelated incoming peer leaves the direct connection's scan alone");
  const int padFd = fixture.accept("AA:BB:CC:DD:EE:02", true);
  check(fixture.connected == std::vector<int>{id} && fixture.accepted.size() == 2,
        "matching ATT accept completes the original outgoing link id");
  check(std::find(fixture.sockets.securityRequests.begin(), fixture.sockets.securityRequests.end(),
                  std::pair<int, int>{padFd, 2}) != fixture.sockets.securityRequests.end(),
        "the accepted outgoing link retains its requested security level");
  std::vector<pollfd> descriptors;
  fixture.radio->collect(descriptors);
  check(fixture.disconnected.empty() && std::any_of(descriptors.begin(), descriptors.end(),
                                                  [&](const pollfd& descriptor) { return descriptor.fd == phone; }),
        "gamepad connection keeps the existing phone socket open");
  check(fixture.sockets.scanRestarted(), "completed direct connection restores the requested scan");
}

void modernKernelUsesL2cap() {
  Fixture fixture(1, "6.8.0");
  fixture.accept();
  Address pad;
  Address::parse("AA:BB:CC:DD:EE:02", false, pad);
  check(fixture.radio->connect(pad, 1) >= 0 && fixture.sockets.connectCalls == 1 &&
            fixture.sockets.countCommand(0x200d) == 0,
        "modern kernels keep their ordinary L2CAP connection path");
}

void directResolvedAddressUsesConnectionHandle() {
  for (const uint16_t handle : {uint16_t{0}, uint16_t{0x42}}) {
    Fixture fixture;
    fixture.accept();
    Address rotating;
    Address::parse("AA:BB:CC:DD:EE:02", true, rotating);
    const int id = fixture.radio->connect(rotating, 1);
    fixture.leConnectionComplete(rotating, 0, handle, true);
    check(fixture.connected.empty(), "successful enhanced LE event waits for the ATT socket");
    Address identity;
    Address::parse("11:22:33:44:55:66", false, identity);
    const int fd = fixture.sockets.queueIncoming(identity);
    fixture.sockets.endpoints.at(fd).connectionHandle = handle;
    fixture.dispatch(fixture.sockets.listener, POLLIN);
    check(fixture.connected == std::vector<int>{id} && fixture.accepted.size() == 1,
          "resolved ATT identity address completes the pending link through its controller handle, including handle zero");
  }
}

void enhancedIdentityMatchesRequestedRpa() {
  Fixture fixture;
  fixture.accept();
  Address rotating;
  Address::parse("AA:BB:CC:DD:EE:02", true, rotating);
  const int id = fixture.radio->connect(rotating, 1);
  Address identity;
  Address::parse("11:22:33:44:55:66", false, identity);
  fixture.leConnectionComplete(identity, 0, 0, true, &rotating);
  const int fd = fixture.sockets.queueIncoming(identity);
  fixture.sockets.endpoints.at(fd).connectionHandle = 0;
  fixture.dispatch(fixture.sockets.listener, POLLIN);
  check(fixture.connected == std::vector<int>{id} && fixture.accepted.size() == 1,
        "enhanced event identity and peer RPA correlate the requested outgoing connection");
}

void controllerFirstStillAcceptsPhone() {
  Fixture fixture;
  Address pad;
  Address::parse("AA:BB:CC:DD:EE:02", false, pad);
  const int id = fixture.radio->connect(pad, 1);
  fixture.dispatch(fixture.sockets.outgoing, POLLOUT);
  const int padFd = fixture.sockets.outgoing;
  fixture.sockets.rawCommands.clear();
  std::string error;
  check(fixture.radio->advertise(1, true, {2, 1, 6}, {}, error),
        "iPhone advertisement starts with a gamepad already connected");
  const bool enable = std::any_of(fixture.sockets.rawCommands.begin(), fixture.sockets.rawCommands.end(),
                                 [](const Bytes& packet) {
                                   return word(packet.data() + 1) == 0x200a && packet.size() == 5 && packet[4] == 1;
                                 });
  check(fixture.sockets.countCommand(0x2006) >= 1 && enable,
        "legacy controller-first order configures and enables advertising through raw HCI");
  const int phoneFd = fixture.accept();
  std::vector<pollfd> descriptors;
  fixture.radio->collect(descriptors);
  check(fixture.connected == std::vector<int>{id} && fixture.accepted.size() == 1 && fixture.disconnected.empty() &&
            std::any_of(descriptors.begin(), descriptors.end(), [&](const pollfd& p) { return p.fd == padFd; }) &&
            std::any_of(descriptors.begin(), descriptors.end(), [&](const pollfd& p) { return p.fd == phoneFd; }),
        "controller-first pairing accepts the phone without closing the gamepad");
}

void cancelledResolvedConnectionIsDiscarded() {
  for (const uint16_t handle : {uint16_t{0}, uint16_t{0x42}}) {
    Fixture fixture;
    fixture.accept();
    Address rotating;
    Address::parse("AA:BB:CC:DD:EE:02", true, rotating);
    const int id = fixture.radio->connect(rotating, 1);
    fixture.radio->disconnect(id);
    Address identity;
    Address::parse("11:22:33:44:55:66", false, identity);
    fixture.leConnectionComplete(identity, 0, handle, true, &rotating);
    const bool disconnectSent = std::any_of(fixture.sockets.rawCommands.begin(), fixture.sockets.rawCommands.end(),
                                           [&](const Bytes& packet) {
                                             return word(packet.data() + 1) == 0x0406 && packet.size() == 7 &&
                                                    word(packet.data() + 4) == handle;
                                           });
    check(disconnectSent, "cancel-race LE success disconnects the established controller handle");
    const int fd = fixture.sockets.queueIncoming(identity);
    fixture.sockets.endpoints.at(fd).connectionHandle = handle;
    fixture.dispatch(fixture.sockets.listener, POLLIN);
    std::vector<pollfd> descriptors;
    fixture.radio->collect(descriptors);
    check(fixture.connected.empty() && fixture.failedConnections.empty() && fixture.accepted.size() == 1 &&
              std::none_of(descriptors.begin(), descriptors.end(), [&](const pollfd& p) { return p.fd == fd; }) &&
              ::fcntl(fd, F_GETFD) < 0 && errno == EBADF,
          "cancel-race resolved ATT socket is discarded, including controller handle zero");
    fixture.commandComplete(0x200e, 0x0c);
    check(fixture.radio->connect(rotating, 1) >= 0 && fixture.sockets.countCommand(0x200d) == 1,
          "cancel-race keeps another initiation blocked until the established handle disconnects");
    fixture.disconnectionComplete(handle);
    fixture.radio->tick(fixture.now);
    check(fixture.sockets.countCommand(0x200d) == 2,
          "cancel-race Disconnection Complete releases initiation serialization");
  }
}

void establishedDirectTimeoutDisconnects() {
  Fixture fixture(1);
  fixture.accept();
  Address pad;
  Address::parse("AA:BB:CC:DD:EE:02", false, pad);
  const int id = fixture.radio->connect(pad, 1);
  fixture.leConnectionComplete(pad, 0, 0);
  fixture.now += 10001;
  fixture.radio->tick(fixture.now);
  check(fixture.sockets.countCommand(0x0406) == 1 && fixture.sockets.countCommand(0x200e) == 0,
        "timeout after LE success disconnects its handle instead of cancelling a finished initiation");
  check(fixture.failedConnections == std::vector<int>{id} && fixture.radio->connect(pad, 1) >= 0 &&
        fixture.sockets.countCommand(0x200d) == 1,
        "timeout after LE success reports once and keeps initiation serialized");
  fixture.disconnectionComplete(0);
  fixture.radio->tick(fixture.now);
  check(fixture.sockets.countCommand(0x200d) == 2, "timed out established handle disconnect permits another initiation");
}

void cancellationEndsAtItsDeadline() {
  for (const bool acknowledged : {true, false}) {
    Fixture fixture;
    fixture.accept();
    Address pad;
    Address::parse("AA:BB:CC:DD:EE:02", false, pad);
    const int id = fixture.radio->connect(pad, 1);
    const int64_t cancelledAt = fixture.now;
    fixture.radio->disconnect(id);
    if (acknowledged) fixture.commandComplete(0x200e);
    fixture.now = cancelledAt + 1999;
    fixture.radio->tick(fixture.now);
    check(fixture.radio->connect(pad, 1) >= 0 && fixture.sockets.countCommand(0x200d) == 1,
          "a cancellation without LE completion holds the next connection for its whole interval");
    ++fixture.now;
    fixture.radio->tick(fixture.now);
    check(fixture.sockets.countCommand(0x200d) == 2,
          acknowledged ? "an acknowledged cancellation without LE completion ends at its deadline"
                       : "a cancellation the controller never answers ends at its deadline");
  }
}

void unconfirmedDisconnectIsAskedOnceMore() {
  Fixture fixture;
  fixture.accept();
  Address pad;
  Address::parse("AA:BB:CC:DD:EE:02", false, pad);
  const int id = fixture.radio->connect(pad, 1);
  fixture.leConnectionComplete(pad, 0, 0x42);
  fixture.radio->disconnect(id);
  check(fixture.sockets.countCommand(0x0406) == 1, "a cancelled link the controller made is disconnected");
  fixture.now += 2000;
  fixture.radio->tick(fixture.now);
  check(fixture.sockets.countCommand(0x0406) == 2 && fixture.radio->connect(pad, 1) >= 0 &&
            fixture.sockets.countCommand(0x200d) == 1,
        "an unconfirmed disconnect is sent once more while the next connection still waits");
  fixture.now += 1999;
  fixture.radio->tick(fixture.now);
  check(fixture.sockets.countCommand(0x200d) == 1, "the second disconnect gets its own interval");
  ++fixture.now;
  fixture.radio->tick(fixture.now);
  check(fixture.sockets.countCommand(0x0406) == 2 && fixture.sockets.countCommand(0x200d) == 2,
        "then the cancellation ends without a third disconnect and the next connection starts");
}

void unsendableCancellationEndsAtOnce() {
  Fixture fixture;
  fixture.accept();
  Address pad;
  Address::parse("AA:BB:CC:DD:EE:02", false, pad);
  const int id = fixture.radio->connect(pad, 1);
  fixture.sockets.unwritable = 0x200e;
  fixture.radio->disconnect(id);
  check(fixture.radio->connect(pad, 1) >= 0 && fixture.sockets.countCommand(0x200d) == 2,
        "a cancel command that cannot be written does not hold up the next connection");
}

void acceptedCentralCanAdvertise() {
  Fixture fixture;
  Address pad;
  Address::parse("AA:BB:CC:DD:EE:02", false, pad);
  fixture.leConnectionComplete(pad, 0, 0);
  const int fd = fixture.sockets.queueIncoming(pad);
  fixture.sockets.endpoints.at(fd).connectionHandle = 0;
  fixture.dispatch(fixture.sockets.listener, POLLIN);
  fixture.sockets.rawCommands.clear();
  std::string error;
  fixture.radio->advertise(1, true, {2, 1, 6}, {}, error);
  const bool enable = std::any_of(fixture.sockets.rawCommands.begin(), fixture.sockets.rawCommands.end(),
                                 [](const Bytes& packet) {
                                   return word(packet.data() + 1) == 0x200a && packet.size() == 5 && packet[4] == 1;
                                 });
  check(enable && fixture.sockets.countCommand(0x2006) >= 1,
        "accepted ATT socket with local central role still permits iPhone advertising");
}

void directDisconnectBeforeAttReportsFailure() {
  Fixture fixture(1);
  fixture.accept();
  Address pad;
  Address::parse("AA:BB:CC:DD:EE:02", false, pad);
  const int id = fixture.radio->connect(pad, 1);
  fixture.leConnectionComplete(pad, 0, 0);
  fixture.disconnectionComplete(0);
  check(fixture.failedConnections == std::vector<int>{id} && fixture.connected.empty(),
        "controller disconnect before ATT setup fails its original pending link");
}

void absentCancelledHandleReleasesInitiation() {
  Fixture fixture;
  fixture.accept();
  Address pad;
  Address::parse("AA:BB:CC:DD:EE:02", false, pad);
  const int id = fixture.radio->connect(pad, 1);
  fixture.leConnectionComplete(pad, 0, 0);
  fixture.radio->disconnect(id);
  fixture.sockets.sendTo(fixture.sockets.scanner, {4, 0x0f, 4, 2, 1, 6, 4});
  fixture.dispatch(fixture.sockets.scanner, POLLIN);
  check(fixture.radio->connect(pad, 1) >= 0 && fixture.failedConnections.empty(),
        "Disconnect Unknown Connection status releases an already absent cancelled handle");
}

void foreignDisconnectStatusLeavesACancellation() {
  Fixture fixture;
  fixture.accept();
  Address pad;
  Address::parse("AA:BB:CC:DD:EE:02", false, pad);
  const int id = fixture.radio->connect(pad, 1);
  fixture.radio->disconnect(id);
  fixture.sockets.sendTo(fixture.sockets.scanner, {4, 0x0f, 4, 2, 1, 6, 4});
  fixture.dispatch(fixture.sockets.scanner, POLLIN);
  check(fixture.radio->connect(pad, 1) >= 0 && fixture.sockets.countCommand(0x200d) == 1,
        "a Disconnect status for another link leaves a cancellation without a handle waiting");
}

void directConnectionRetries() {
  Fixture fixture;
  fixture.accept();
  Address pad;
  Address::parse("AA:BB:CC:DD:EE:02", false, pad);
  const int id = fixture.radio->connect(pad, 1);
  for (int attempt = 1; attempt <= 3; ++attempt) {
    fixture.sockets.sendTo(fixture.sockets.scanner, {4, 0x0f, 4, 0x0c, 1, 0x0d, 0x20});
    fixture.dispatch(fixture.sockets.scanner, POLLIN);
    if (attempt == 3) break;
    check(fixture.failedConnections.empty(), "retryable direct failure keeps the original connection pending");
    fixture.now += 999;
    fixture.radio->tick(fixture.now);
    check(fixture.sockets.countCommand(0x200d) == static_cast<std::size_t>(attempt),
          "direct retry respects its delay");
    ++fixture.now;
    fixture.radio->tick(fixture.now);
    check(fixture.sockets.countCommand(0x200d) == static_cast<std::size_t>(attempt + 1),
          "direct retry initiates the next controller attempt");
  }
  check(fixture.failedConnections == std::vector<int>{id}, "exhausted direct retries report a single failure for the original id");
}

void directFailuresAndCancellation() {
  Address pad;
  Address::parse("AA:BB:CC:DD:EE:02", false, pad);
  {
    Fixture fixture(1);
    fixture.accept();
    fixture.radio->scan(true, true, true);
    const int id = fixture.radio->connect(pad, 1);
    fixture.sockets.sendTo(fixture.sockets.scanner, {4, 0x0f, 4, 0x0c, 1, 0x0d, 0x20});
    fixture.dispatch(fixture.sockets.scanner, POLLIN);
    check(fixture.failedConnections == std::vector<int>{id}, "rejected direct connection reports failure for its original id");
  }
  {
    Fixture fixture(1);
    fixture.accept();
    fixture.radio->scan(true, true, true);
    const int id = fixture.radio->connect(pad, 1);
    fixture.now += 10001;
    fixture.radio->tick(fixture.now);
    check(fixture.sockets.countCommand(0x200e) == 1, "timed out direct connection sends LE Create Connection Cancel");
    fixture.commandComplete(0x200e);
    fixture.leConnectionComplete(pad, 2);
    check(fixture.failedConnections == std::vector<int>{id}, "timed out direct connection reports a single failure");
  }
  {
    Fixture fixture;
    fixture.accept();
    fixture.radio->scan(true, true, true);
    const int id = fixture.radio->connect(pad, 1);
    fixture.sockets.commands.clear();
    fixture.radio->disconnect(id);
    check(fixture.sockets.countCommand(0x200e) == 1, "closing a pending direct connection cancels the controller procedure");
    const int queued = fixture.radio->connect(pad, 1);
    check(queued >= 0 && fixture.sockets.commands.empty(),
          "cancellation retains serialization and leaves the controller scan alone");
    fixture.commandComplete(0x200e);
    fixture.radio->tick(fixture.now);
    check(fixture.sockets.commands.empty(),
          "Cancel Command Complete alone does not start a second connection or restore scanning");
    fixture.leConnectionComplete(pad, 2);
    check(fixture.sockets.scanRestarted(), "cancelled LE completion restores the requested scan");
    check(fixture.connected.empty() && fixture.failedConnections.empty(), "explicit cancellation produces no stale connection callback");
    fixture.radio->tick(fixture.now);
    check(fixture.sockets.countCommand(0x200d) == 2,
          "completed cancellation starts the queued connection");
  }
}

void hciErrorsAreVisible() {
  Fixture fixture;
  fixture.radio->scan(true, true, true);
  auto expectDiagnostic = [&](const Bytes& packet, const char* opcode, const char* what) {
    const std::size_t before = fixture.logs.size();
    fixture.sockets.sendTo(fixture.sockets.scanner, packet);
    fixture.dispatch(fixture.sockets.scanner, POLLIN);
    const bool found = std::any_of(fixture.logs.begin() + static_cast<std::ptrdiff_t>(before), fixture.logs.end(),
                                   [&](const std::string& line) { return line.find(opcode) != std::string::npos; });
    check(found, what);
  };
  expectDiagnostic({4, 0x0e, 4, 1, 0x0c, 0x20, 0x0c}, "200c", "failed scan Command Complete is logged with its opcode");
  expectDiagnostic({4, 0x0f, 4, 0x0c, 1, 0x0b, 0x20}, "200b", "failed scan Command Status is logged with its opcode");
  const std::size_t before = fixture.logs.size();
  fixture.sockets.sendTo(fixture.sockets.scanner, {4, 0x0e, 4, 1, 0x0c, 0x20, 0});
  fixture.sockets.sendTo(fixture.sockets.scanner, {4, 0x0f, 4, 0, 1, 0x0b, 0x20});
  fixture.dispatch(fixture.sockets.scanner, POLLIN);
  check(fixture.logs.size() == before, "successful scan commands do not produce error diagnostics");
}

void outgoingConnectionsAreQueued() {
  Fixture fixture(1, "6.8.0");
  Address first, second;
  Address::parse("AA:BB:CC:DD:EE:02", false, first);
  Address::parse("AA:BB:CC:DD:EE:03", false, second);
  const int one = fixture.radio->connect(first, 1);
  const int firstFd = fixture.sockets.outgoing;
  const int two = fixture.radio->connect(second, 1);
  check(one >= 0 && two > one && fixture.sockets.connectCalls == 1, "second controller queues with a stable id");
  for (int i = 0; i < 20; ++i) { fixture.now += 1000; fixture.radio->tick(fixture.now); }
  check(fixture.sockets.connectCalls == 1 && fixture.failedConnections.empty(), "waiting consumes no connection attempts");
  fixture.dispatch(firstFd, POLLOUT);
  fixture.radio->tick(fixture.now);
  check(fixture.sockets.connectCalls == 2 && fixture.connected == std::vector<int>{one},
        "second connection starts after the first is established");
  fixture.dispatch(fixture.sockets.outgoing, POLLOUT);
  check(fixture.connected == std::vector<int>({one, two}) && fixture.disconnected.empty(),
        "both controllers stay connected, even with only one attempt allowed");
}

void cancelledQueuedConnectionDoesNotTouchTheActiveOne() {
  Fixture fixture(1, "6.8.0");
  Address first, second;
  Address::parse("AA:BB:CC:DD:EE:02", false, first);
  Address::parse("AA:BB:CC:DD:EE:03", false, second);
  const int one = fixture.radio->connect(first, 1);
  const int firstFd = fixture.sockets.outgoing;
  const int two = fixture.radio->connect(second, 1);
  fixture.radio->disconnect(two);
  fixture.dispatch(firstFd, POLLOUT);
  fixture.radio->tick(fixture.now);
  check(fixture.connected == std::vector<int>{one} && fixture.sockets.connectCalls == 1,
        "removing a queued request leaves the active procedure alone");
}

void newConnectionsCannotOvertakeTheQueue() {
  for (const std::string release : {"6.8.0", "4.9.84"}) {
    Fixture fixture(1, release);
    const bool direct = release == "4.9.84";
    if (direct) fixture.accept();
    Address first, second, third;
    Address::parse("AA:BB:CC:DD:EE:02", false, first);
    Address::parse("AA:BB:CC:DD:EE:03", false, second);
    Address::parse("AA:BB:CC:DD:EE:04", false, third);
    const int one = fixture.radio->connect(first, 1);
    const int firstFd = fixture.sockets.outgoing;
    const int two = fixture.radio->connect(second, 1);
    if (direct) fixture.accept(first.str().c_str());
    else fixture.dispatch(firstFd, POLLOUT);
    // An advert or script can request another link between dispatch and the next radio tick.
    const int three = fixture.radio->connect(third, 1);
    check((direct ? fixture.sockets.countCommand(0x200d) : fixture.sockets.connectCalls) == 1,
          "a new request cannot start ahead of an older queued request after dispatch");
    fixture.radio->tick(fixture.now);
    if (direct) fixture.accept(second.str().c_str());
    else fixture.dispatch(fixture.sockets.outgoing, POLLOUT);
    check(fixture.connected == std::vector<int>({one, two}),
          "the oldest queued controller starts before a newly arriving controller");
    fixture.radio->tick(fixture.now);
    if (direct) fixture.accept(third.str().c_str());
    else fixture.dispatch(fixture.sockets.outgoing, POLLOUT);
    check(fixture.connected == std::vector<int>({one, two, three}) && fixture.failedConnections.empty(),
          "normal and legacy connection procedures both preserve request order and stable ids");
  }
}

void connectionRetriesDoNotBlockReadyRequests() {
  Fixture fixture(2, "6.8.0");
  Address first, second, third;
  Address::parse("AA:BB:CC:DD:EE:02", false, first);
  Address::parse("AA:BB:CC:DD:EE:03", false, second);
  Address::parse("AA:BB:CC:DD:EE:04", false, third);
  const int one = fixture.radio->connect(first, 1);
  const int firstFd = fixture.sockets.outgoing;
  const int two = fixture.radio->connect(second, 1);
  fixture.sockets.connectError = ECONNRESET;
  fixture.dispatch(firstFd, POLLOUT);
  const int64_t retryAt = fixture.now + 1000;
  fixture.sockets.connectError = 0;
  fixture.radio->tick(fixture.now);
  check(fixture.sockets.connectCalls == 2 && fixture.failedConnections.empty(),
        "a failed attempt rejoins the queue and lets an older waiting request start");
  const int three = fixture.radio->connect(third, 1);
  fixture.dispatch(fixture.sockets.outgoing, POLLOUT);
  fixture.radio->tick(fixture.now);
  fixture.dispatch(fixture.sockets.outgoing, POLLOUT);
  fixture.now = retryAt - 1;
  fixture.radio->tick(fixture.now);
  check(fixture.connected == std::vector<int>({two, three}) && fixture.sockets.connectCalls == 3,
        "retry backoff leaves other ready requests runnable without starting the retry early");
  ++fixture.now;
  fixture.radio->tick(fixture.now);
  fixture.dispatch(fixture.sockets.outgoing, POLLOUT);
  check(fixture.connected == std::vector<int>({two, three, one}) && fixture.sockets.connectCalls == 4 &&
            fixture.failedConnections.empty(),
        "the retry starts when eligible, keeping its id and its remaining attempt");
}

void cancelledQueueHeadLetsTheNextRequestRun() {
  Fixture fixture(1, "6.8.0");
  Address first, second, third;
  Address::parse("AA:BB:CC:DD:EE:02", false, first);
  Address::parse("AA:BB:CC:DD:EE:03", false, second);
  Address::parse("AA:BB:CC:DD:EE:04", false, third);
  const int one = fixture.radio->connect(first, 1);
  const int firstFd = fixture.sockets.outgoing;
  const int two = fixture.radio->connect(second, 1);
  const int three = fixture.radio->connect(third, 1);
  fixture.radio->disconnect(two);
  fixture.dispatch(firstFd, POLLOUT);
  fixture.radio->tick(fixture.now);
  fixture.dispatch(fixture.sockets.outgoing, POLLOUT);
  check(fixture.connected == std::vector<int>({one, three}) && fixture.sockets.connectCalls == 2 &&
            fixture.failedConnections.empty(),
        "removing the head of the queue releases the next request without a stale callback");
}

// A command in the worker's first pass still gets the full attach time.
void attachTimeCountsFromTheRequest() {
  SocketController sockets;
  sockets.controllerAttached = false;
  controller = &sockets;
  std::vector<std::string> lines;
  int request = -1;
  {
    BleService::Options options;
    options.supervised = true;
    options.gamepad = false;
    BleService service(options, [](awtrix::ble::BleEvent) {});
    awtrix::ble::IphoneConfig config;
    config.enabled = true;
    service.configureIphone(config);
    service.start();
    for (int i = 0; i < 100 && request < 0; ++i) {
      request = service.takeControllerRequest();
      if (request < 0) std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
    std::this_thread::sleep_for(std::chrono::milliseconds(200));
    service.drainLog([&](const std::string& line) { lines.push_back(line); });
    service.stop();
  }
  controller = nullptr;
  check(request == 1, "the iPhone link asks the supervisor for the controller");
  check(std::none_of(lines.begin(), lines.end(),
                     [](const std::string& line) { return line.find("did not appear") != std::string::npos; }),
        "the controller is not given up before its attach time has passed");
}

}  // namespace

extern "C" int __wrap_socket(int family, int type, int protocol) {
  if (controller && family == kBluetooth) return controller->socket(protocol);
  return __real_socket(family, type, protocol);
}

extern "C" int __wrap_bind(int fd, const sockaddr* address, socklen_t length) {
  if (!controller || !controller->endpoints.count(fd)) return __real_bind(fd, address, length);
  auto& endpoint = controller->endpoints.at(fd);
  if (endpoint.protocol == kHci && length >= sizeof(HciAddress)) {
    const auto* hci = reinterpret_cast<const HciAddress*>(address);
    endpoint.kind = hci->channel == 3 ? Kind::Management : Kind::Scanner;
    if (endpoint.kind == Kind::Scanner) controller->scanner = fd;
  }
  return 0;
}

extern "C" int __wrap_listen(int fd, int backlog) {
  if (!controller || !controller->endpoints.count(fd)) return __real_listen(fd, backlog);
  controller->endpoints.at(fd).kind = Kind::Listener;
  controller->listener = fd;
  return 0;
}

extern "C" int __wrap_setsockopt(int fd, int level, int option, const void* value, socklen_t length) {
  if (controller && controller->endpoints.count(fd)) {
    if (level == kBluetoothLevel && option == kSecurity && length >= 1)
      controller->securityRequests.emplace_back(fd, static_cast<const uint8_t*>(value)[0]);
    return 0;
  }
  return __real_setsockopt(fd, level, option, value, length);
}

extern "C" int __wrap_getsockopt(int fd, int level, int option, void* value, socklen_t* length) {
  if (!controller || !controller->endpoints.count(fd)) return __real_getsockopt(fd, level, option, value, length);
  if (level == SOL_SOCKET && option == SO_ERROR && *length >= sizeof(int)) {
    *static_cast<int*>(value) = controller->connectError;
    *length = sizeof(int);
    return 0;
  }
  if (level == kBluetoothLevel && option == kSecurity && *length >= 2) {
    static_cast<uint8_t*>(value)[0] = 1;
    static_cast<uint8_t*>(value)[1] = 0;
    *length = 2;
    return 0;
  }
  if (level == 6 && option == 2 && *length >= 2) {
    std::memset(value, 0, *length);
    *static_cast<uint16_t*>(value) = controller->endpoints.at(fd).connectionHandle;
    return 0;
  }
  errno = ENOPROTOOPT;
  return -1;
}

extern "C" int __wrap_connect(int fd, const sockaddr* address, socklen_t length) {
  if (!controller || !controller->endpoints.count(fd)) return __real_connect(fd, address, length);
  controller->endpoints.at(fd).kind = Kind::Link;
  controller->outgoing = fd;
  ++controller->connectCalls;
  errno = EINPROGRESS;
  return -1;
}

extern "C" int __wrap_accept4(int fd, sockaddr* address, socklen_t* length, int flags) {
  if (!controller || !controller->endpoints.count(fd)) return __real_accept4(fd, address, length, flags);
  if (controller->incoming.empty()) {
    errno = EAGAIN;
    return -1;
  }
  char marker;
  (void)!::read(fd, &marker, 1);
  const Incoming incoming = controller->incoming.front();
  controller->incoming.pop_front();
  L2Address peer{};
  peer.family = kBluetooth;
  peer.cid = 4;
  peer.type = incoming.address.random ? 2 : 1;
  std::copy(incoming.address.b.begin(), incoming.address.b.end(), peer.address);
  std::memcpy(address, &peer, std::min(static_cast<std::size_t>(*length), sizeof peer));
  *length = sizeof peer;
  return incoming.fd;
}

extern "C" ssize_t __wrap_write(int fd, const void* value, size_t length) {
  if (!controller || !controller->endpoints.count(fd)) return __real_write(fd, value, length);
  const auto& endpoint = controller->endpoints.at(fd);
  const auto* data = static_cast<const uint8_t*>(value);
  if (endpoint.kind == Kind::Management && length >= 6) {
    controller->answerManagement(fd, data);
    return static_cast<ssize_t>(length);
  }
  if (endpoint.kind == Kind::Scanner && length >= 4 && data[0] == 1) {
    if (word(data + 1) == controller->unwritable) {
      errno = EIO;
      return -1;
    }
    controller->rawCommands.emplace_back(data, data + length);
    if (word(data + 1) == kScanParameters || word(data + 1) == kScanEnable)
      controller->commands.emplace_back(data, data + length);
    return static_cast<ssize_t>(length);
  }
  return __real_write(fd, value, length);
}

extern "C" int __wrap_uname(utsname* information) {
  const int result = __real_uname(information);
  if (result == 0 && controller) {
    std::strncpy(information->release, controller->kernelRelease.c_str(), sizeof information->release - 1);
    information->release[sizeof information->release - 1] = '\0';
  }
  return result;
}

int main() {
  steadyTimingUsesTheSocketHandle();
  pairingMethodMetadataContainsNoKeyMaterial();
  secureConnectionsConfiguration();
  localBleIdentityConfiguration();
  connectionParameterDiagnostics();
  outgoingConnectionsAreQueued();
  cancelledQueuedConnectionDoesNotTouchTheActiveOne();
  newConnectionsCannotOvertakeTheQueue();
  connectionRetriesDoNotBlockReadyRequests();
  cancelledQueueHeadLetsTheNextRequestRun();
  incomingRestoresScan();
  pendingConnectOwnsScan();
  disabledScanStaysOff();
  directConnectionPreservesPhone();
  modernKernelUsesL2cap();
  directResolvedAddressUsesConnectionHandle();
  enhancedIdentityMatchesRequestedRpa();
  controllerFirstStillAcceptsPhone();
  cancelledResolvedConnectionIsDiscarded();
  establishedDirectTimeoutDisconnects();
  cancellationEndsAtItsDeadline();
  unconfirmedDisconnectIsAskedOnceMore();
  unsendableCancellationEndsAtOnce();
  acceptedCentralCanAdvertise();
  directDisconnectBeforeAttReportsFailure();
  absentCancelledHandleReleasesInitiation();
  foreignDisconnectStatusLeavesACancellation();
  directConnectionRetries();
  directFailuresAndCancellation();
  hciErrorsAreVisible();
  attachTimeCountsFromTheRequest();
  if (failures) std::printf("%d Linux BLE radio checks failed\n", failures);
  return failures ? 1 : 0;
}
