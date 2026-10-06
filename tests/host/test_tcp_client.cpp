#include "WiFiClient.h"
#include "../support.h"

#include <chrono>
#include <cstring>
#include <thread>

using awtrix::test::require;
using awtrix::posix::UniqueFd;

namespace {
struct Server {
  UniqueFd fd{::socket(AF_INET, SOCK_STREAM | SOCK_CLOEXEC, 0)};
  uint16_t port = 0;
  Server() {
    require(fd.valid(), "create loopback server");
    sockaddr_in address{};
    address.sin_family = AF_INET;
    address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    require(::bind(fd.get(), reinterpret_cast<sockaddr*>(&address), sizeof address) == 0 &&
            ::listen(fd.get(), 4) == 0, "bind and listen");
    socklen_t size = sizeof address;
    require(::getsockname(fd.get(), reinterpret_cast<sockaddr*>(&address), &size) == 0, "read port");
    port = ntohs(address.sin_port);
  }
  UniqueFd accept() {
    pollfd pending{fd.get(), POLLIN, 0};
    require(::poll(&pending, 1, 1000) == 1, "connection reaches listener");
    UniqueFd peer{::accept4(fd.get(), nullptr, nullptr, SOCK_CLOEXEC)};
    require(peer.valid(), "accept client");
    return peer;
  }
};

template <typename Predicate> void await(Predicate predicate, const char* message) {
  const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(1);
  while (!predicate()) {
    require(std::chrono::steady_clock::now() < deadline, message);
    std::this_thread::sleep_for(std::chrono::milliseconds(1));
  }
}
}

int main() {
  Server server;
  WiFiClient client;
  require(client.connect(IPAddress(127, 0, 0, 1), server.port) == 1, "connect numeric IPv4");
  auto peer = server.accept();
  require(client.connected() && client.available() == 0 && client.read() == -1,
          "connected socket without input is nonblocking");
  uint8_t bytes[8]{};
  require(client.read(bytes, sizeof bytes) == 0, "empty bulk read reports no data");
  require(::send(peer.get(), "abc", 3, MSG_NOSIGNAL) == 3, "peer sends bytes");
  await([&] { return client.available() == 3; }, "incoming bytes become available");
  require(client.connected() && client.peek() == 'a' && client.peek() == 'a',
          "connection probe and peek preserve buffered data");
  require(client.read() == 'a' && client.read(bytes, 2) == 2 && std::memcmp(bytes, "bc", 2) == 0,
          "single and bulk reads consume exactly their bytes");
  require(client.write(reinterpret_cast<const uint8_t*>("reply"), 5) == 5, "write returns sent length");
  pollfd incoming{peer.get(), POLLIN, 0};
  require(::poll(&incoming, 1, 1000) == 1 && ::recv(peer.get(), bytes, sizeof bytes, 0) == 5 &&
          std::memcmp(bytes, "reply", 5) == 0, "peer receives client bytes");
  require(::shutdown(peer.get(), SHUT_WR) == 0, "peer closes its output");
  await([&] { return !client.connected(); }, "peer closure is detected");
  require(client.read(bytes, sizeof bytes) == -1, "closed socket bulk read reports EOF");

  require(client.connect("localhost", server.port) == 1, "hostname reconnect uses IPv4");
  peer = server.accept();
  client.stop();
  require(!client.connected() && !client && client.available() == 0, "stop resets the client");
  incoming = {peer.get(), POLLIN, 0};
  require(::poll(&incoming, 1, 1000) == 1 && ::recv(peer.get(), bytes, sizeof bytes, 0) == 0,
          "stop closes the peer connection");
  server.fd.reset();
  require(client.connect("127.0.0.1", server.port) == 0 && !client.connected(),
          "refused connect never becomes connected merely because it is writable");
  require(client.connect(nullptr, server.port) == 0 && client.connect("::1", server.port) == 0,
          "invalid and unsupported addresses fail");
  require(awtrix::posix::connectionError(-1) == EBADF, "descriptor error is preserved");
  return awtrix::test::finish("host TCP client");
}
