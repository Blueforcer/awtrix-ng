#pragma once

#include <cstddef>
#include <cstdint>

#include "core/net/Datagram.h"

namespace awtrix {

class UdpSocket {
 public:
  ~UdpSocket() { close(); }

  bool open(uint16_t port);
  void close();
  bool isOpen() const { return fd_ >= 0; }

  int receive(void* buf, std::size_t cap);
  int receiveFrom(void* buf, std::size_t cap, net::Endpoint& from);

  bool replyTo(uint16_t port, const void* data, std::size_t len);
  bool sendTo(const net::Endpoint& to, const void* data, std::size_t len);

 private:
  int fd_ = -1;
  uint32_t peerAddr_ = 0;
  bool havePeer_ = false;
};

}
