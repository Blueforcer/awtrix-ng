#include "transport/net/UdpSocket.h"

#if defined(ESP_PLATFORM)
#include <lwip/sockets.h>

#include <cerrno>
#else
#include "platform/host/Socket.h"
#endif

#include <cstring>

namespace awtrix {

namespace {

#if defined(ESP_PLATFORM)
[[gnu::always_inline]] inline void closeUdp(std::intptr_t fd) { ::close(static_cast<int>(fd)); }

[[gnu::always_inline]] inline int receiveNow(std::intptr_t fd, void* buf, std::size_t cap, sockaddr_in& peer) {
  socklen_t peerLen = sizeof(peer);
  return ::recvfrom(static_cast<int>(fd), buf, cap, MSG_DONTWAIT, reinterpret_cast<sockaddr*>(&peer), &peerLen);
}

[[gnu::always_inline]] inline bool nothingWaiting() { return errno == EWOULDBLOCK || errno == EAGAIN; }

[[gnu::always_inline]] inline bool sendNow(std::intptr_t fd, const void* data, std::size_t len, const sockaddr_in& to,
                                           bool wait) {
  return ::sendto(static_cast<int>(fd), data, len, wait ? 0 : MSG_DONTWAIT, reinterpret_cast<const sockaddr*>(&to),
                  sizeof(to)) == static_cast<int>(len);
}
#else
std::intptr_t openUdp(uint16_t port) {
  const os::Socket socket = os::openUdp(port);
  return socket == os::kNoSocket ? -1 : static_cast<std::intptr_t>(socket);
}

void closeUdp(std::intptr_t fd) { os::closeSocket(static_cast<os::Socket>(fd)); }

int receiveNow(std::intptr_t fd, void* buf, std::size_t cap, sockaddr_in& peer) {
  return static_cast<int>(os::receiveDatagram(static_cast<os::Socket>(fd), buf, cap, peer));
}

bool nothingWaiting() { return os::wouldBlock(os::socketError()); }

bool sendNow(std::intptr_t fd, const void* data, std::size_t len, const sockaddr_in& to, bool) {
  return os::sendDatagram(static_cast<os::Socket>(fd), data, len, to) == static_cast<long>(len);
}
#endif

}

bool UdpSocket::open(uint16_t port) {
  close();
#if defined(ESP_PLATFORM)
  fd_ = ::socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
  if (fd_ < 0) return false;

  int on = 1;
  ::setsockopt(fd_, SOL_SOCKET, SO_REUSEADDR, &on, sizeof(on));

  sockaddr_in addr{};
  addr.sin_family = AF_INET;
  addr.sin_addr.s_addr = htonl(INADDR_ANY);
  addr.sin_port = htons(port);
  if (::bind(fd_, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) < 0) {
    close();
    return false;
  }
  return true;
#else
  fd_ = openUdp(port);
  return fd_ >= 0;
#endif
}

void UdpSocket::close() {
  if (fd_ >= 0) closeUdp(fd_);
  fd_ = -1;
  havePeer_ = false;
  peerAddr_ = 0;
}

// Non-blocking. Returns 0 when nothing is waiting and -1 on a real error, so a caller can loop
// until it gets 0 without ever stalling the main loop.
int UdpSocket::receive(void* buf, std::size_t cap) {
  net::Endpoint from;
  const int n = receiveFrom(buf, cap, from);
  if (n > 0) {
    peerAddr_ = htonl(from.address);
    havePeer_ = true;
  }
  return n;
}

int UdpSocket::receiveFrom(void* buf, std::size_t cap, net::Endpoint& from) {
  if (fd_ < 0 || cap == 0) return -1;
  sockaddr_in peer{};
  const int n = receiveNow(fd_, buf, cap, peer);
  if (n < 0) return nothingWaiting() ? 0 : -1;
  from.address = ntohl(peer.sin_addr.s_addr);
  from.port = ntohs(peer.sin_port);
  return n;
}

// Never blocks: a datagram the stack has no buffer for right now is dropped.
bool UdpSocket::sendTo(const net::Endpoint& to, const void* data, std::size_t len) {
  if (fd_ < 0) return false;
  sockaddr_in peer{};
  peer.sin_family = AF_INET;
  peer.sin_addr.s_addr = htonl(to.address);
  peer.sin_port = htons(to.port);
  return sendNow(fd_, data, len, peer, false);
}

// Answers whoever sent the most recent datagram, on the given port. Only valid after receive() has
// returned a packet.
bool UdpSocket::replyTo(uint16_t port, const void* data, std::size_t len) {
  if (fd_ < 0 || !havePeer_) return false;
  sockaddr_in to{};
  to.sin_family = AF_INET;
  to.sin_addr.s_addr = peerAddr_;
  to.sin_port = htons(port);
  return sendNow(fd_, data, len, to, true);
}

}
