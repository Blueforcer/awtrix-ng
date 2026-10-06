#pragma once

#include <cstddef>
#include <cstdint>

namespace awtrix {
namespace net {

// An IPv4 address and UDP port, both in host byte order.
struct Endpoint {
  uint32_t address = 0;
  uint16_t port = 0;

  bool operator==(const Endpoint& other) const {
    return address == other.address && port == other.port;
  }
  bool operator!=(const Endpoint& other) const { return !(*this == other); }
};

class IDatagramSink {
 public:
  virtual ~IDatagramSink() = default;
  // Never blocks. A datagram the network cannot take right now is dropped and reported as false.
  virtual bool send(const Endpoint& to, const uint8_t* data, std::size_t length) = 0;
};

}
}
