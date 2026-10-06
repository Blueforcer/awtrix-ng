#include <PubSubClient.h>
#include <algorithm>
#include <cstdio>
#include <deque>
#include <stdexcept>
#include <string>
#include <vector>

namespace {
unsigned cases = 0, callbacks = 0;
std::string receivedTopic;
std::vector<uint8_t> receivedPayload;

void require(bool condition, const char* message) {
  if (!condition) throw std::runtime_error(message);
}

void message(char* topic, uint8_t* payload, unsigned int length) {
  ++callbacks;
  receivedTopic = topic;
  receivedPayload.assign(payload, payload + length);
}

class Socket final : public Client {
 public:
  std::deque<uint8_t> input{0x20, 0x02, 0x00, 0x00};
  std::vector<uint8_t> output;
  bool active = false;
  int connect(IPAddress, uint16_t) override { active = true; return 1; }
  int connect(const char*, uint16_t) override { active = true; return 1; }
  size_t write(uint8_t value) override { output.push_back(value); return 1; }
  size_t write(const uint8_t* values, size_t size) override {
    output.insert(output.end(), values, values + size); return size;
  }
  int available() override { return static_cast<int>(input.size()); }
  int read() override {
    if (input.empty()) return -1;
    const auto value = input.front(); input.pop_front(); return value;
  }
  int read(uint8_t* buffer, size_t size) override {
    const size_t count = std::min(size, input.size());
    for (size_t i = 0; i < count; ++i) buffer[i] = static_cast<uint8_t>(read());
    return static_cast<int>(count);
  }
  int peek() override { return input.empty() ? -1 : input.front(); }
  void flush() override {}
  void stop() override { active = false; }
  uint8_t connected() override { return active; }
  operator bool() override { return active; }
};

class Sink final : public Stream {
 public:
  std::vector<uint8_t> received;
  size_t write(uint8_t value) override { received.push_back(value); return 1; }
  int available() override { return 0; }
  int read() override { return -1; }
  int peek() override { return -1; }
};

struct Fixture {
  Socket socket;
  PubSubClient client{socket};
  Fixture() {
    callbacks = 0; receivedTopic.clear(); receivedPayload.clear();
    client.setServer(IPAddress(127, 0, 0, 1), 1883);
    client.setBufferSize(8192);
    client.setSocketTimeout(0); // Truncation times out immediately.
    client.setCallback(message);
    require(client.connect("fixture"), "fixture MQTT connection");
    socket.output.clear();
  }
  void receive(const std::vector<uint8_t>& packet) { socket.input.insert(socket.input.end(), packet.begin(), packet.end()); }
};

std::vector<uint8_t> publish(const std::string& topic, const std::vector<uint8_t>& payload, bool qos1 = false) {
  std::vector<uint8_t> packet{static_cast<uint8_t>(qos1 ? 0x32 : 0x30)};
  size_t remaining = 2 + topic.size() + (qos1 ? 2 : 0) + payload.size();
  do {
    uint8_t digit = remaining % 128;
    remaining /= 128;
    packet.push_back(digit | (remaining ? 128 : 0));
  } while (remaining);
  packet.push_back(static_cast<uint8_t>(topic.size() >> 8));
  packet.push_back(static_cast<uint8_t>(topic.size()));
  packet.insert(packet.end(), topic.begin(), topic.end());
  if (qos1) { packet.push_back(0); packet.push_back(7); }
  packet.insert(packet.end(), payload.begin(), payload.end());
  return packet;
}

void validPackets() {
  for (const bool qos1 : {false, true}) {
    for (const std::vector<uint8_t>& payload : {std::vector<uint8_t>{}, std::vector<uint8_t>{'o', 'k'},
                                             std::vector<uint8_t>(8175, 0x55)}) {
      Fixture fixture;
      fixture.receive(publish("contract/x", payload, qos1));
      require(fixture.client.loop(), "valid packet disconnected");
      require(callbacks == 1 && receivedTopic == "contract/x" && receivedPayload == payload, "valid packet callback changed");
      if (qos1) require(fixture.socket.output == std::vector<uint8_t>({0x40, 2, 0, 7}), "QoS1 acknowledgement changed");
      else require(fixture.socket.output.empty(), "QoS0 unexpectedly acknowledged");
      ++cases;
    }
  }
}

void invalidPackets() {
  const std::vector<std::vector<uint8_t>> malformed{
      {0x30, 0x02, 0xff, 0xff}, // Topic length exceeds the packet.
      {0x30, 0x00}, {0x30, 0x01, 0x00}, {0x30, 0x02, 0x00, 0x00},
      {0x30, 0x04, 0x00, 0x05, 'a', 'b'},
      {0x32, 0x03, 0x00, 0x01, 'a'}, // missing QoS1 packet ID
      {0x32, 0x04, 0x00, 0x01, 'a', 0}, // truncated packet ID
      {0x34, 0x05, 0x00, 0x01, 'a', 0, 7}, // unsupported QoS2
      {0x36, 0x05, 0x00, 0x01, 'a', 0, 7}, // reserved QoS
      {0x30, 0x80, 0x80, 0x80, 0x80, 0x00}, // malformed remaining length
      {0x30, 0x03, 0x00}, {0x30, 0x05, 0x00, 0x01, 'a'}, // truncated body
      {0x30, 0xff, 0xff, 0xff, 0x7f} // huge body must be rejected without reading it
  };
  for (const auto& packet : malformed) {
    for (const bool callback : {true, false}) {
      Fixture fixture;
      if (!callback) fixture.client.setCallback(nullptr);
      fixture.receive(packet);
      require(!fixture.client.loop(), "malformed packet accepted");
      require(!fixture.socket.active && callbacks == 0, "malformed packet reached callback or stayed connected");
      ++cases;
    }
  }
  Fixture tiny;
  tiny.client.setBufferSize(4);
  tiny.receive({0x30, 0x03, 0x00, 0x01, 'a'});
  require(!tiny.client.loop() && callbacks == 0, "tiny buffer overflow");
  ++cases;
}

void streamedPackets() {
  Fixture fixture;
  Sink sink;
  fixture.client.setBufferSize(32);
  fixture.client.setStream(sink);
  const std::vector<uint8_t> payload(4096, 0x5a);
  fixture.receive(publish("topic", payload));
  require(fixture.client.loop() && sink.received == payload, "valid streamed payload changed");
  ++cases;
  Fixture tooLong;
  tooLong.client.setBufferSize(32);
  tooLong.client.setStream(sink);
  tooLong.receive(publish(std::string(64, 'x'), {}));
  require(!tooLong.client.loop() && callbacks == 0, "streamed topic overflow");
  ++cases;
}
}

int main() {
  try {
    validPackets();
    invalidPackets();
    streamedPackets();
    std::printf("MQTT parser: %u valid/malformed cases passed\n", cases);
    return 0;
  } catch (const std::exception& error) {
    std::fprintf(stderr, "MQTT parser regression failed: %s (case %u)\n", error.what(), cases + 1);
    return 1;
  }
}
