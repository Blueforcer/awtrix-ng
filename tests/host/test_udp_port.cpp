#include "transport/net/UdpPort.h"
#include "../support.h"

#include "../Sockets.h"

#include <chrono>
#include <cstring>
#include <thread>
#include <vector>

using awtrix::net::Endpoint;
using awtrix::net::UdpPort;
using awtrix::net::kMaxDatagram;
using awtrix::test::check;
using awtrix::test::require;

namespace {
struct Socket {
  awtrix::os::Socket fd = ::socket(AF_INET, SOCK_DGRAM, 0);
  uint16_t port = 0;
  explicit Socket(uint32_t address) {
    require(fd != awtrix::os::kNoSocket, "create a UDP socket");
    sockaddr_in at{};
    at.sin_family = AF_INET;
    at.sin_addr.s_addr = htonl(address);
    require(::bind(fd, reinterpret_cast<sockaddr*>(&at), sizeof at) == 0, "bind a UDP socket");
    socklen_t size = sizeof at;
    require(::getsockname(fd, reinterpret_cast<sockaddr*>(&at), &size) == 0, "read its port");
    port = ntohs(at.sin_port);
  }
  ~Socket() { close(); }
  void close() {
    awtrix::os::closeSocket(fd);
    fd = awtrix::os::kNoSocket;
  }
  void sendTo(uint16_t to, std::size_t length) {
    const std::vector<uint8_t> bytes(length, static_cast<uint8_t>(length));
    sockaddr_in at{};
    at.sin_family = AF_INET;
    at.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    at.sin_port = htons(to);
    require(awtrix::os::sendDatagram(fd, bytes.data(), length, at) == static_cast<long>(length),
            "send a datagram");
  }
};

std::vector<std::size_t> received(UdpPort& port, std::size_t count, Endpoint& from) {
  std::vector<std::size_t> lengths;
  const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(1);
  while (lengths.size() < count && std::chrono::steady_clock::now() < deadline) {
    if (const std::size_t length = port.receive(from)) {
      check(port.data()[0] == static_cast<uint8_t>(length) && port.data()[length - 1] == static_cast<uint8_t>(length),
            "the datagram's bytes are handed over");
      lengths.push_back(length);
    } else {
      std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
  }
  return lengths;
}
}

int main() {
  require(awtrix::os::startSockets(), "sockets");
  Socket blocker(INADDR_ANY);
  const uint16_t number = blocker.port;
  UdpPort port;
  port.keepOpen(number, 1000);
  require(!port.isOpen(), "a port held by another socket does not open");
  blocker.close();
  port.keepOpen(number, 1000 + UdpPort::kRetryMs - 1);
  check(!port.isOpen(), "a failed open is not tried again within five seconds");
  port.keepOpen(number, 1000 + UdpPort::kRetryMs);
  require(port.isOpen(), "a failed open is tried again after five seconds");

  Socket peer(INADDR_LOOPBACK);
  peer.sendTo(number, 5);
  peer.sendTo(number, kMaxDatagram + 1);
  peer.sendTo(number, kMaxDatagram);
  Endpoint from;
  const std::vector<std::size_t> lengths = received(port, 2, from);
  check(lengths == std::vector<std::size_t>{5, kMaxDatagram}, "datagrams longer than the longest are dropped");
  check(from.address == INADDR_LOOPBACK && from.port == peer.port, "the sender is reported");
  check(port.receive(from) == 0, "nothing more is waiting");

  const uint8_t reply[] = {'o', 'k'};
  check(port.send({INADDR_LOOPBACK, peer.port}, reply, sizeof reply), "a datagram goes out");
  uint8_t back[4] = {};
  awtrix::test::receiveTimeout(peer.fd, 1000);
  check(awtrix::os::receiveBytes(peer.fd, back, sizeof back) == 2 && std::memcmp(back, reply, 2) == 0,
        "the peer gets it");

  check(port.close(), "closing an open port says so");
  check(!port.isOpen() && port.data() == nullptr, "a closed port holds no buffer");
  check(!port.close(), "closing a closed port says nothing was open");
  check(port.receive(from) == 0, "a closed port receives nothing");
  port.keepOpen(number, 1000 + UdpPort::kRetryMs + 1);
  check(port.isOpen(), "a closed port opens again at once");
  return awtrix::test::finish("UDP port");
}
