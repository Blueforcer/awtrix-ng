#pragma once
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

namespace awtrix::tc002::voice {
// A single asynchronous network owner. Calls from the render thread only
// enqueue; neither DNS, TLS, socket writes nor a reconnect can block panel
// rendering. TLS peers are verified against the process trust store
// (tls/TlsTrust); without a loaded store a TLS connect fails.
class WebSocket {
 public:
  struct Endpoint {
    std::string host, port, target;
    bool tls = false;
  };
  struct Event {
    enum Kind { Open, Text, Closed, Failed } kind = Failed;
    uint64_t generation = 0;
    std::string text;
  };
  WebSocket();
  ~WebSocket();
  WebSocket(const WebSocket&) = delete;
  WebSocket& operator=(const WebSocket&) = delete;
  uint64_t connect(const Endpoint& endpoint);
  void cancel();
  bool send(std::string bytes, bool binary = false);
  std::vector<Event> poll();

 private:
  struct Impl;
  std::unique_ptr<Impl> impl_;
};
}  // namespace awtrix::tc002::voice
