#include "transport/net/UdpSocket.h"

#if !defined(AWTRIX_PLATFORM_RP2040)
#include <lwip/sockets.h>
#endif

#include <cstring>

namespace awtrix {

#if defined(AWTRIX_PLATFORM_RP2040)
bool UdpSocket::open(uint16_t port) {
  close();
  fd_ = udp_.begin(port) ? 0 : -1;
  return isOpen();
}
void UdpSocket::close() {
  udp_.stop();
  fd_ = -1;
  havePeer_ = false;
}
int UdpSocket::receive(void* buf, std::size_t cap) {
  if (!isOpen() || !cap) return -1;
  if (udp_.parsePacket() <= 0) return 0;
  peerAddr_ = static_cast<uint32_t>(udp_.remoteIP());
  havePeer_ = true;
  const int n = udp_.read(static_cast<unsigned char*>(buf), cap);
  while (udp_.available()) udp_.read();
  return n;
}
bool UdpSocket::replyTo(uint16_t port, const void* data, std::size_t len) {
  if (!isOpen() || !havePeer_ || !udp_.beginPacket(IPAddress(peerAddr_), port)) return false;
  const auto n = udp_.write(static_cast<const uint8_t*>(data), len);
  return udp_.endPacket() && n == len;
}
#else

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
#endif

}
