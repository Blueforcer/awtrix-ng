#pragma once

#include <cstddef>
#include <cstdint>
#include <memory>

#include "core/net/Datagram.h"
#include "transport/net/UdpSocket.h"

namespace awtrix {
namespace net {

class UdpPort final : public IDatagramSink {
 public:
  static constexpr int64_t kRetryMs = 5000;

  void keepOpen(uint16_t port, int64_t nowMs);
  bool close();
  bool isOpen() const { return socket_.isOpen(); }
  std::size_t receive(Endpoint& from);
  const uint8_t* data() const { return rx_.get(); }
  bool send(const Endpoint& to, const uint8_t* data, std::size_t length) override;

 private:
  UdpSocket socket_;
  std::unique_ptr<uint8_t[]> rx_;
  int64_t retryAtMs_ = 0;
};

}
}
