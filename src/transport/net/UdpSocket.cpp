#include "transport/net/UdpSocket.h"

#if defined(ESP_PLATFORM)
#include <lwip/sockets.h>
#else
#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>
#endif

#include <cerrno>
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
  socklen_t peerLen = sizeof(peer);
  const int n = ::recvfrom(fd_, buf, cap, MSG_DONTWAIT,
                           reinterpret_cast<sockaddr*>(&peer), &peerLen);
  if (n < 0) {
    return (errno == EWOULDBLOCK || errno == EAGAIN) ? 0 : -1;
  }
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
  return ::sendto(fd_, data, len, MSG_DONTWAIT, reinterpret_cast<sockaddr*>(&peer),
                  sizeof(peer)) == static_cast<int>(len);
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
