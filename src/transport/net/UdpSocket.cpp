#include "transport/net/UdpSocket.h"

#include <lwip/sockets.h>

#include <cstring>

namespace awtrix {

bool UdpSocket::open(uint16_t port) {
  close();
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
}

void UdpSocket::close() {
  if (fd_ >= 0) ::close(fd_);
  fd_ = -1;
  havePeer_ = false;
  peerAddr_ = 0;
}

// Non-blocking. Returns 0 when nothing is waiting and -1 on a real error, so a caller can loop
// until it gets 0 without ever stalling the main loop.
int UdpSocket::receive(void* buf, std::size_t cap) {
  if (fd_ < 0 || cap == 0) return -1;
  sockaddr_in from{};
  socklen_t fromLen = sizeof(from);
  const int n = ::recvfrom(fd_, buf, cap, MSG_DONTWAIT,
                           reinterpret_cast<sockaddr*>(&from), &fromLen);
  if (n < 0) {
    return (errno == EWOULDBLOCK || errno == EAGAIN) ? 0 : -1;
  }
  peerAddr_ = from.sin_addr.s_addr;
  havePeer_ = true;
  return n;
}

// Answers whoever sent the most recent datagram, on the given port. Only valid after receive() has
// returned a packet.
bool UdpSocket::replyTo(uint16_t port, const void* data, std::size_t len) {
  if (fd_ < 0 || !havePeer_) return false;
  sockaddr_in to{};
  to.sin_family = AF_INET;
  to.sin_addr.s_addr = peerAddr_;
  to.sin_port = htons(port);
  return ::sendto(fd_, data, len, 0, reinterpret_cast<sockaddr*>(&to), sizeof(to)) ==
         static_cast<int>(len);
}

}
