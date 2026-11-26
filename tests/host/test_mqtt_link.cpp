#include "transport/mqtt/MqttLink.h"

#include <algorithm>
#include <array>
#include <chrono>
#include <cstdio>
#include <cstring>
#include <initializer_list>
#include <string>
#include <utility>
#include <vector>

#include "../support.h"

namespace {
constexpr auto check = awtrix::test::require;
uint32_t fakeNow = 0;

class Resolver final : public awtrix::net::IHostResolver {
 public:
  awtrix::net::ResolveState resolve(const std::string&) override {
    ++requests;
    if (!cached) { current = next; cached = true; }
    return awtrix::net::ResolveState::Ready;
  }
  uint32_t address() const override { return current; }
  awtrix::net::LinkError error() const override { return awtrix::net::LinkError::None; }
  void forget() override { cached = false; }
  uint32_t next = 0x7f00002a;
  unsigned requests = 0;
 private:
  uint32_t current = 0;
  bool cached = false;
};

class Broker final : public Client {
 public:
  int connect(IPAddress address, uint16_t port) override {
    peer = address.toString();
    peerPort = port;
    ++attempts;
    connected_ = accept;
    ready_ = false;
    offset_ = 0;
    return connected_;
  }
  int connect(const char*, uint16_t) override { return 0; }
  size_t write(uint8_t value) override { return write(&value, 1); }
  size_t write(const uint8_t* data, size_t size) override {
    if (size && data[0] == 0xe0) disconnectedGracefully = true;
    if (!connected_) return 0;
    if (size && data[0] == 0x10) {
      ready_ = true;
      answerAt_ = fakeNow + answerDelayMs;
      ack_[3] = returnCode;
      connectPacket.assign(data, data + size);
    }
    return size;
  }
  int available() override {
    return ready_ && answers && fakeNow >= answerAt_ ? static_cast<int>(ack_.size() - offset_) : 0;
  }
  int read() override { uint8_t value; return read(&value, 1) == 1 ? value : -1; }
  int read(uint8_t* out, size_t size) override {
    const auto count = std::min(size, static_cast<size_t>(available()));
    std::memcpy(out, ack_.data() + offset_, count);
    offset_ += count;
    return static_cast<int>(count);
  }
  int peek() override { return available() ? ack_[offset_] : -1; }
  void flush() override {}
  void stop() override { connected_ = false; }
  uint8_t connected() override { return connected_; }
  operator bool() override { return connected_; }
  bool accept = true, answers = true, disconnectedGracefully = false;
  unsigned attempts = 0;
  uint32_t answerDelayMs = 0;
  uint8_t returnCode = 0;
  std::vector<uint8_t> connectPacket;
  std::string peer;
  uint16_t peerPort = 0;
 private:
  std::array<uint8_t, 4> ack_{0x20, 0x02, 0x00, 0x00};
  uint32_t answerAt_ = 0;
  size_t offset_ = 0;
  bool ready_ = false, connected_ = false;
};

class Transport final : public awtrix::IMqttTransport {
 public:
  Client& socket() override { return broker; }
  void setPeerName(const std::string&) override {}
  awtrix::net::ResolveState connectStep(std::function<bool()> handshake) override {
    if (pending) return awtrix::net::ResolveState::Pending;
    return handshake() ? awtrix::net::ResolveState::Ready : awtrix::net::ResolveState::Failed;
  }
  void shutdown() override { pending = false; broker.stop(); }
  Broker broker;
  bool pending = false;
};

struct Rig {
  explicit Rig(const awtrix::DeviceConfig& config) {
    link.begin(config, "link-test", "test", &resolver, &status, &transport);
  }
  bool tick(uint32_t now) {
    fakeNow = now;
    return link.tick(now, true);
  }
  Resolver resolver;
  Transport transport;
  awtrix::net::LinkStatus status;
  awtrix::MqttLink link;
};

std::vector<uint8_t> connectPacket(uint8_t flags, std::initializer_list<std::string> fields) {
  std::vector<uint8_t> body{0, 4, 'M', 'Q', 'T', 'T', 4, flags, 0, 15};
  for (const auto& field : fields) {
    body.push_back(static_cast<uint8_t>(field.size() >> 8));
    body.push_back(static_cast<uint8_t>(field.size()));
    body.insert(body.end(), field.begin(), field.end());
  }
  std::vector<uint8_t> packet{0x10, static_cast<uint8_t>(body.size())};
  packet.insert(packet.end(), body.begin(), body.end());
  return packet;
}
}

int main() {
  using namespace awtrix;
  using net::LinkError;
  using net::LinkPhase;
  DeviceConfig config;
  config.mqttEnabled = true;
  config.mqttHost = "broker.example";
  config.mqttPort = 1884;
  Resolver resolver;
  Transport transport;
  net::LinkStatus status;
  MqttLink link;
  unsigned onlineEvents = 0;
  link.begin(config, "link-test", "test", &resolver, &status, &transport);
  link.setOnOnline([&] { ++onlineEvents; });

  check(!link.tick(0, false) && resolver.requests == 0 && transport.broker.attempts == 0,
        "offline network does not attempt DNS or MQTT");
  check(status.error == LinkError::NoWifi && status.retryInMs == 0, "offline network is reported");
  check(link.tick(1, true) && status.phase == LinkPhase::Connected && onlineEvents == 1,
        "connected network opens MQTT");
  check(transport.broker.peer == "127.0.0.42" && transport.broker.peerPort == 1884 &&
        status.endpoint == "127.0.0.42:1884", "resolved address reaches PubSubClient without byte reversal");

  check(!link.tick(2, false) && !link.online(), "network loss closes an otherwise live broker connection");
  check(!transport.broker.disconnectedGracefully, "network loss preserves the broker last-will behavior");
  resolver.next = 0x7f00002b;
  check(link.tick(3, true) && transport.broker.peer == "127.0.0.43" && onlineEvents == 2,
        "network restoration refreshes DNS and sends the online event again");

  check(!link.tick(4, false), "second network loss");
  transport.broker.accept = false;
  check(!link.tick(5, true) && status.attempts == 1 && status.retryInMs > 0,
        "refused broker starts backoff");
  const unsigned attempts = transport.broker.attempts;
  check(!link.tick(6, true) && transport.broker.attempts == attempts, "backoff prevents a repeated attempt");
  check(!link.tick(7, false), "offline during backoff");
  transport.broker.accept = true;
  check(link.tick(8, true) && onlineEvents == 3, "new network association retries immediately");

  check(!link.tick(9, false), "offline before asynchronous handshake");
  transport.pending = true;
  check(!link.tick(10, true) && status.phase == LinkPhase::Connecting, "platform handshake may be pending");
  check(!link.tick(11, false) && !link.online(), "network loss cancels the pending handshake");
  check(link.tick(12, true) && onlineEvents == 4, "cancelled handshake does not block reconnection");
  config.mqttPort = 65535;
  resolver.next = 0xffffffff;
  resolver.forget();
  net::LinkStatus longestStatus;
  Transport longestTransport;
  MqttLink longest;
  longest.begin(config, "longest-endpoint", "test", &resolver, &longestStatus, &longestTransport);
  check(longest.tick(13, true) && longestStatus.endpoint == "255.255.255.255:65535",
        "maximum IPv4 address and port fit the public endpoint");

  DeviceConfig base;
  base.mqttEnabled = true;
  base.mqttHost = "broker.example";
  {
    Rig fast(base);
    check(fast.tick(1000) && fast.status.phase == LinkPhase::Connected,
          "a broker that answers at once connects in the same tick");
    check(fast.transport.broker.connectPacket ==
              connectPacket(0x26, {"link-test", "test/availability", "offline"}),
          "anonymous CONNECT carries the retained offline will and a clean session");
    DeviceConfig login = base;
    login.mqttUser = "user";
    login.mqttPass = "secret";
    Rig user(login);
    check(user.tick(1000) && user.transport.broker.connectPacket ==
              connectPacket(0xe6, {"link-test", "test/availability", "offline", "user", "secret"}),
          "CONNECT with credentials");
    login.mqttUser.clear();
    Rig passOnly(login);
    check(passOnly.tick(1000) && passOnly.transport.broker.connectPacket ==
              connectPacket(0xe6, {"link-test", "test/availability", "offline", "", "secret"}),
          "a password without a user still sends both fields");
  }
  {
    Rig slow(base);
    slow.transport.broker.answerDelayMs = 5000;
    auto longestTick = std::chrono::steady_clock::duration::zero();
    uint32_t connectedAt = 0;
    bool connecting = true;
    for (uint32_t now = 1000; now <= 20000 && !connectedAt; now += 20) {
      const auto start = std::chrono::steady_clock::now();
      if (slow.tick(now)) connectedAt = now;
      longestTick = std::max(longestTick, std::chrono::steady_clock::now() - start);
      connecting = connecting && (connectedAt || (slow.status.phase == LinkPhase::Connecting &&
                                                  slow.status.retryInMs == 0));
    }
    check(connectedAt == 6000 && slow.status.phase == LinkPhase::Connected &&
              slow.transport.broker.attempts == 1,
          "a CONNACK after 5 s connects after 5 s in one attempt");
    check(connecting, "the link reports connecting while it waits");
    check(longestTick < std::chrono::milliseconds(100), "no tick waits for the CONNACK");
  }
  {
    Rig silent(base);
    silent.transport.broker.answers = false;
    check(!silent.tick(1000) && !silent.tick(10999) &&
              silent.status.phase == LinkPhase::Connecting && silent.status.attempts == 0,
          "a silent broker is waited on for 10 s");
    check(!silent.tick(11000) && silent.status.phase == LinkPhase::Offline &&
              silent.status.error == LinkError::Timeout &&
              silent.link.client()->state() == MQTT_CONNECTION_TIMEOUT,
          "and then times out");
    check(silent.status.attempts == 1 && silent.status.retryInMs > 0 &&
              !silent.transport.broker.connected(),
          "the timeout closes the socket and arms the backoff");
    const uint32_t retryAt = 11000 + silent.status.retryInMs;
    check(!silent.tick(retryAt - 1) && silent.transport.broker.attempts == 1,
          "no attempt during the backoff");
    silent.transport.broker.answers = true;
    check(silent.tick(retryAt) && silent.transport.broker.attempts == 2,
          "the attempt after the backoff connects");
  }
  {
    Rig lost(base);
    lost.transport.broker.answers = false;
    check(!lost.tick(1000), "waiting for a CONNACK");
    fakeNow = 2000;
    check(!lost.link.tick(2000, false) && lost.status.error == LinkError::NoWifi,
          "network loss while waiting");
    lost.transport.broker.answers = true;
    check(lost.tick(3000) && lost.transport.broker.attempts == 2,
          "the next network starts a fresh attempt");
  }
  const std::pair<uint8_t, LinkError> refusals[] = {
      {MQTT_CONNECT_BAD_PROTOCOL, LinkError::Rejected},
      {MQTT_CONNECT_BAD_CLIENT_ID, LinkError::Rejected},
      {MQTT_CONNECT_UNAVAILABLE, LinkError::Rejected},
      {MQTT_CONNECT_BAD_CREDENTIALS, LinkError::BadCredentials},
      {MQTT_CONNECT_UNAUTHORIZED, LinkError::BadCredentials}};
  for (const auto& [code, error] : refusals) {
    Rig refused(base);
    refused.transport.broker.returnCode = code;
    refused.transport.broker.answerDelayMs = 3000;
    check(!refused.tick(1000) && !refused.tick(4000) && refused.status.error == error &&
              refused.link.client()->state() == code && refused.status.attempts == 1 &&
              refused.status.retryInMs > 0 && !refused.transport.broker.connected(),
          "CONNACK code " + std::to_string(code) + " maps to its error and backs off");
  }
  std::puts("mqtt link: network loss, refresh, backoff, pending handshake and CONNACK wait passed");
}
