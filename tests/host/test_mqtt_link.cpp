#include "transport/mqtt/MqttLink.h"

#include <algorithm>
#include <array>
#include <cstdio>
#include <cstring>

#include "../support.h"

namespace {
constexpr auto check = awtrix::test::require;

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
    if (size && data[0] == 0x10) ready_ = true;
    return size;
  }
  int available() override { return ready_ ? static_cast<int>(ack_.size() - offset_) : 0; }
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
  bool accept = true, disconnectedGracefully = false;
  unsigned attempts = 0;
  std::string peer;
  uint16_t peerPort = 0;
 private:
  const std::array<uint8_t, 4> ack_{0x20, 0x02, 0x00, 0x00};
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
  std::puts("mqtt link: network loss, refresh, backoff and pending-handshake recovery passed");
}
