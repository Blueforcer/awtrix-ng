#include "../../support.h"
#include "../../../test/EngineFakes.h"
#include <cerrno>
#include <chrono>
#include <csignal>
#include <cstdio>
#include <cstdlib>
#include <fcntl.h>
#include <string>
#include <sys/socket.h>
#include <unistd.h>
#include <vector>


#include "core/CoreEngine.h"
#include "core/api/JsonWriter.h"
#include "persistence/DeviceConfig.h"
#include "platform/linux/LinuxBoard.h"
#include "platform/tc002/runtime/SupervisedRuntime.h"
#include "platform/tc002/runtime/SupervisorLink.h"

using namespace awtrix;

namespace {
using awtrix::test::require;

struct Pair {
  int runtime = -1, supervisor = -1;
  explicit Pair(int type = SOCK_SEQPACKET) {
    int ends[2];
    require(::socketpair(AF_UNIX, type | SOCK_CLOEXEC, 0, ends) == 0, "socketpair");
    runtime = ends[0];
    supervisor = ends[1];
  }
  ~Pair() {
    if (runtime >= 0) ::close(runtime);
    if (supervisor >= 0) ::close(supervisor);
  }
  int take() { const int fd = runtime; runtime = -1; return fd; }
};

std::string receive(int fd) {
  char buffer[4096];
  const ssize_t n = ::recv(fd, buffer, sizeof(buffer), MSG_DONTWAIT);
  return n > 0 ? std::string(buffer, static_cast<size_t>(n)) : std::string();
}

std::vector<tc002::SupervisorMessage> drain(SupervisorLink& link, bool& open) {
  std::vector<tc002::SupervisorMessage> messages;
  open = link.poll([&](const tc002::SupervisorMessage& message) { messages.push_back(message); });
  return messages;
}

void descriptors() {
  std::string error;
  int ends[2];
  require(::pipe(ends) == 0, "pipe");
  {
    SupervisorLink link;
    require(!link.open(ends[0], error), "pipe refused");
  }
  ::close(ends[0]);
  ::close(ends[1]);
  for (int type : {SOCK_STREAM, SOCK_DGRAM}) {
    Pair pair(type);
    SupervisorLink link;
    require(!link.open(pair.runtime, error), "only SOCK_SEQPACKET accepted");
  }
  {
    const int unconnected = ::socket(AF_UNIX, SOCK_SEQPACKET, 0);
    SupervisorLink link;
    require(!link.open(unconnected, error), "unconnected socket refused");
    ::close(unconnected);
  }
  Pair pair;
  SupervisorLink link;
  const int fd = pair.take();
  require(link.open(fd, error), "connected SEQPACKET accepted");
  require((::fcntl(fd, F_GETFL) & O_NONBLOCK) && (::fcntl(fd, F_GETFD) & FD_CLOEXEC), "nonblocking, not inherited");
  require(!link.open(fd, error), "second open refused");
}

void exchange() {
  Pair pair;
  SupervisorLink link;
  std::string error;
  require(link.open(pair.take(), error), "open");
  bool open = false;
  const auto started = std::chrono::steady_clock::now();
  require(drain(link, open).empty() && open, "idle poll delivers nothing");
  require(std::chrono::steady_clock::now() - started < std::chrono::milliseconds(20), "idle poll does not wait");

  require(link.send(tc002::encodeHello("1.2.3")), "hello sent");
  tc002::SupervisorMessage message;
  require(tc002::decodeSupervisorMessage(receive(pair.supervisor), message) &&
          message.type == tc002::MessageType::Hello && message.version == "1.2.3", "one hello datagram");

  tc002::PowerStatus power;
  power.usbPower = true;
  power.batteryPercent = 55;
  const std::string good = tc002::encodePower(power);
  const std::string oversize(tc002::kMaxSupervisorMessage + 100, ' ');
  for (const std::string& datagram : {std::string("{\"v\":2,\"type\":\"nonsense\"}"), oversize, good})
    require(::send(pair.supervisor, datagram.data(), datagram.size(), 0) == static_cast<ssize_t>(datagram.size()),
            "supervisor send");
  const auto messages = drain(link, open);
  require(open && messages.size() == 1 && messages[0].type == tc002::MessageType::Power &&
          messages[0].power.batteryPercent == 55, "invalid datagrams skipped, valid one delivered");
  require(link.refused() == 2, "refusals counted");
  require(!link.send(std::string()) && !link.send(oversize), "empty and oversize output refused");
}

void backpressure() {
  Pair pair;
  SupervisorLink link;
  std::string error;
  const int fd = pair.runtime;
  require(link.open(pair.take(), error), "open");
  const std::string filler(tc002::kMaxSupervisorMessage, 'x');
  unsigned filled = 0;
  while (::send(fd, filler.data(), filler.size(), MSG_DONTWAIT) > 0) ++filled;
  require(errno == EAGAIN || errno == EWOULDBLOCK, "peer buffer full");
  const std::string last = tc002::encodeReboot();
  for (std::size_t i = 0; i < SupervisorLink::kMaxQueued; ++i)
    require(link.send(i + 1 == SupervisorLink::kMaxQueued ? last : tc002::encodeNtp("pool.ntp.org")), "queued");
  require(!link.send(tc002::encodeNtp("x")), "bounded queue refuses more");
  const auto started = std::chrono::steady_clock::now();
  require(!link.flush(100), "flush gives up while the peer does not read");
  require(std::chrono::steady_clock::now() - started < std::chrono::milliseconds(500), "flush is bounded");
  for (unsigned i = 0; i < filled; ++i) require(receive(pair.supervisor) == filler, "filler");
  std::vector<std::string> delivered;
  const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(2);
  while (delivered.size() < SupervisorLink::kMaxQueued && std::chrono::steady_clock::now() < deadline) {
    bool open = false;
    drain(link, open);
    require(open, "channel stays open");
    for (std::string datagram; !(datagram = receive(pair.supervisor)).empty();) delivered.push_back(datagram);
  }
  require(delivered.size() == SupervisorLink::kMaxQueued && delivered.back() == last, "queued output delivered in order");
  require(link.flush(100), "nothing left to flush");
}

void closing() {
  Pair pair;
  SupervisorLink link;
  std::string error;
  require(link.open(pair.take(), error), "open");
  tc002::TimeStatus time;
  time.synchronized = true;
  const std::string datagram = tc002::encodeTime(time);
  require(::send(pair.supervisor, datagram.data(), datagram.size(), 0) > 0, "last message");
  ::close(pair.supervisor);
  pair.supervisor = -1;
  bool open = true;
  const auto messages = drain(link, open);
  require(messages.size() == 1 && messages[0].type == tc002::MessageType::Time, "pending message still delivered");
  require(!open && link.closed(), "peer close ends the channel");
  require(!link.send(tc002::encodeReboot()), "nothing is sent after close");
  require(!link.poll([](const tc002::SupervisorMessage&) {}), "stays closed");
}

void configuration() {
  SupervisedSettings applied{"Home", "pool.ntp.org", "", {}};
  DeviceConfig next;
  next.wifiSsid = "Home";
  next.ntpServer = "pool.ntp.org";
  next.hostname = "";
  require(supervisorConfigChanges(applied, next).empty(), "unchanged configuration sends nothing");

  next.wifiPass = "correct horse";
  auto datagrams = supervisorConfigChanges(applied, next);
  tc002::SupervisorMessage message;
  require(datagrams.size() == 1 && tc002::decodeSupervisorMessage(datagrams[0], message) &&
          message.type == tc002::MessageType::Wifi && message.wifi.ssid == "Home" &&
          message.wifi.password == "correct horse", "new password forwarded");
  require(next.wifiPass.empty(), "password never stays in the configuration");

  next.wifiSsid = "Cafe";
  require(supervisorConfigChanges(applied, next).empty() && next.wifiSsid == "Home" && applied.wifiSsid == "Home",
          "new SSID with a blank password keeps the current network");

  next.wifiSsid = "Cafe";
  datagrams = supervisorConfigChanges(applied, next, true);
  require(datagrams.size() == 1 && tc002::decodeSupervisorMessage(datagrams[0], message) &&
          message.wifi.ssid == "Cafe" && message.wifi.password.empty(), "setup accepts an open network");

  next.wifiSsid = "Cafe";
  next.wifiPass = "espresso";
  datagrams = supervisorConfigChanges(applied, next);
  require(datagrams.size() == 1 && tc002::decodeSupervisorMessage(datagrams[0], message) &&
          message.wifi.ssid == "Cafe" && message.wifi.password == "espresso", "new SSID forwarded with its password");
  require(supervisorConfigChanges(applied, next).empty(), "applied SSID is remembered");

  const std::string hexPsk = "0123456789abcdefABCDEF0123456789abcdef0123456789abcdef0123456789";
  next.wifiSsid = "Hex";
  next.wifiPass = hexPsk;
  datagrams = supervisorConfigChanges(applied, next);
  require(datagrams.size() == 1 && datagrams[0].find("\"password\":\"" + hexPsk + "\"") != std::string::npos &&
          applied.wifiSsid == "Hex", "64-digit hex PSK forwarded");

  for (const std::string& refused : {std::string("short"), std::string(64, 'g'), std::string(65, 'a'),
                                     std::string("tab\there!")}) {
    next.wifiSsid = "Elsewhere";
    next.wifiPass = refused;
    require(supervisorConfigChanges(applied, next).empty() && next.wifiSsid == "Hex" && next.wifiPass.empty() &&
            applied.wifiSsid == "Hex", "password the supervisor refuses keeps the current network");
  }

  next.wifiSsid.clear();
  require(supervisorConfigChanges(applied, next).empty() && next.wifiSsid == "Hex",
          "cleared SSID keeps the current network");

  next.ntpServer = "de.pool.ntp.org";
  next.hostname = "kitchen";
  datagrams = supervisorConfigChanges(applied, next);
  require(datagrams.size() == 2 && tc002::decodeSupervisorMessage(datagrams[0], message) &&
          message.type == tc002::MessageType::Ntp && message.ntpServer == "de.pool.ntp.org",
          "NTP server forwarded");
  require(tc002::decodeSupervisorMessage(datagrams[1], message) &&
          message.type == tc002::MessageType::Hostname && message.hostname == "kitchen", "hostname forwarded");

  next.ip = "192.168.1.50";
  next.subnet = "255.255.255.0";
  require(supervisorConfigChanges(applied, next).empty(), "an ip without netStatic stays on DHCP");
  next.netStatic = true;
  datagrams = supervisorConfigChanges(applied, next);
  require(datagrams.size() == 1 && tc002::decodeSupervisorMessage(datagrams[0], message) &&
          message.type == tc002::MessageType::Address && message.address.enabled &&
          message.address.ip == "192.168.1.50" && message.address.subnet == "255.255.255.0",
          "static address forwarded");
  require(supervisorConfigChanges(applied, next).empty(), "applied address is remembered");
  next.ip.clear();
  datagrams = supervisorConfigChanges(applied, next);
  require(datagrams.size() == 1 && tc002::decodeSupervisorMessage(datagrams[0], message) &&
          !message.address.enabled, "netStatic without an ip means DHCP");

  next.wifiSsid = std::string(33, 'a');
  next.wifiPass = "long enough";
  require(supervisorConfigChanges(applied, next).empty() && next.wifiPass.empty() && next.wifiSsid == "Hex",
          "oversize SSID not forwarded, password still dropped");
}

void association() {
  tc002::NetworkStatus network;
  network.link = tc002::WifiLink::Connected;
  require(wifiAssociation(network) == net::WifiAssoc::Joining, "associated without address is still joining");
  network.ipv4 = "192.168.1.20";
  require(wifiAssociation(network) == net::WifiAssoc::Connected, "connected with address");
  network.link = tc002::WifiLink::Connecting;
  require(wifiAssociation(network) == net::WifiAssoc::Joining, "connecting");
  network.link = tc002::WifiLink::Failed;
  require(wifiAssociation(network) == net::WifiAssoc::AuthFailed, "failed join");
  network.link = tc002::WifiLink::Disconnected;
  require(wifiAssociation(network) == net::WifiAssoc::Disconnected, "disconnected");
}

using NullDisplay = awtrix::test::NullDisplay;

using NullSystem = awtrix::test::NullSystem;

void runtimeState() {
  sound::AudioRouter audio;
  NullDisplay display;
  NullSystem system;
  CoreEngine engine(audio, display, system);
  LinuxBoard board(52, 16);
  DeviceConfig config;
  config.wifiSsid = "Home";
  SupervisedPageClock clock;
  SupervisorLink link;
  SupervisedRuntime supervision(link, engine, board, config, clock);
  const RuntimeState& rt = engine.state().runtime();
  supervision.start();
  require(!supervision.networkConnected(), "no network before the supervisor reports one, loopback notwithstanding");
  int powerEvents = 0;
  engine.state().subscribe([&](StateEvent event) { if (event == StateEvent::PowerChanged) ++powerEvents; });
  auto members = [&] {
    std::string out;
    api::JsonWriter json(out);
    json.beginObject();
    supervision.writeMembers(json);
    json.endObject();
    return out;
  };
  require(members() == "{}", "no USB state before the supervisor reports power");

  tc002::SupervisorMessage message;
  message.type = tc002::MessageType::Power;
  message.power.usbPower = true;
  supervision.apply(message);
  require(rt.externalPower, "USB supply reaches the runtime before the battery is known");
  require(members() == "{\"usbPower\":true}", "device state reports the USB supply");
  require(powerEvents == 1, "the first power report pushes device state");
  supervision.apply(message);
  require(powerEvents == 1, "an unchanged supply pushes nothing");
  message.power = {false, 64, 3900};
  supervision.apply(message);
  require(!rt.externalPower && rt.batteryPercent == 64, "supply and battery follow each report");
  require(members() == "{\"usbPower\":false}" && powerEvents == 2, "unplugging is reported and pushed");

  auto facts = [&] {
    DeviceFacts out;
    supervision.addFacts(out);
    return out;
  };
  require(!facts().hasMacAddress, "no MAC address before the supervisor reports the network");
  message.type = tc002::MessageType::Network;
  message.network.link = tc002::WifiLink::AccessPoint;
  message.network.mac = "a4:cf:12:0b:3c:7d";
  supervision.apply(message);
  const DeviceFacts hotspot = facts();
  require(hotspot.hasMacAddress && hotspot.macAddress[0] == 0xA4 && hotspot.macAddress[5] == 0x7D,
          "the station MAC address is known in the setup hotspot");
  message.network.mac = "a4:cf:12:0b:3c";
  supervision.apply(message);
  require(!facts().hasMacAddress, "a malformed MAC address is left out");
  message.network.mac.clear();

  message.type = tc002::MessageType::Network;
  message.network.link = tc002::WifiLink::Connected;
  message.network.ssid = "Home";
  message.network.rssi = -61;
  supervision.apply(message);
  require(!supervision.networkConnected(), "an association without an address is not a usable network");
  message.network.ipv4 = "192.0.2.100";
  supervision.apply(message);
  require(supervision.networkConnected(), "the supervisor's address makes the network usable");
  require(rt.wifiRssi == -61 && rt.wifi.endpoint == "192.0.2.100", "signal and address while connected");
  message.network.link = tc002::WifiLink::Disconnected;
  message.network.ipv4.clear();
  supervision.apply(message);
  require(rt.wifiRssi == 0 && rt.wifi.endpoint.empty(), "no signal while disconnected");
  require(!supervision.networkConnected(), "a lost connection is reported");
}
}

int main() {
  std::signal(SIGPIPE, SIG_IGN);
  descriptors();
  exchange();
  backpressure();
  closing();
  configuration();
  association();
  runtimeState();
  std::puts("supervisor link: ok");
  return 0;
}
