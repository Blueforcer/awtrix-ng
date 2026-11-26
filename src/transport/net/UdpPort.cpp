#include "transport/net/UdpPort.h"

#include <new>

namespace awtrix {
namespace net {

namespace {
constexpr std::size_t kCapacity = kMaxDatagram + 1;
}

void UdpPort::keepOpen(uint16_t port, int64_t nowMs) {
  if (socket_.isOpen() || nowMs < retryAtMs_) return;
  if (!rx_) rx_.reset(new (std::nothrow) uint8_t[kCapacity]);
  if (!rx_ || !socket_.open(port)) retryAtMs_ = nowMs + kRetryMs;
}

bool UdpPort::close() {
  if (!socket_.isOpen()) return false;
  socket_.close();
  rx_.reset();
  return true;
}

std::size_t UdpPort::receive(Endpoint& from) {
  int length;
  while ((length = socket_.receiveFrom(rx_.get(), kCapacity, from)) > 0)
    if (static_cast<std::size_t>(length) < kCapacity) return static_cast<std::size_t>(length);
  return 0;
}

bool UdpPort::send(const Endpoint& to, const uint8_t* data, std::size_t length) {
  return socket_.sendTo(to, data, length);
}

}
}
