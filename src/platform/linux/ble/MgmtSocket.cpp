#include "platform/linux/ble/MgmtSocket.h"

#include <poll.h>
#include <sys/socket.h>
#include <unistd.h>

#include "platform/linux/ble/LinuxBleProtocol.h"

namespace awtrix::ble {
using namespace linux_detail;

bool MgmtSocket::open() {
  if (fd_.get() >= 0) return true;
  fd_.reset(::socket(kAfBluetooth, SOCK_RAW | SOCK_CLOEXEC | SOCK_NONBLOCK, kProtoHci));
  if (fd_.get() < 0) return false;
  SockaddrHci a{static_cast<sa_family_t>(kAfBluetooth), kNoDevice, kChannelControl};
  if (::bind(fd_.get(), reinterpret_cast<sockaddr*>(&a), sizeof a) < 0) {
    fd_.reset();
    return false;
  }
  return true;
}

// Sends one command and waits for its answer; other traffic that arrives meanwhile is kept and
// handled afterwards. Returns the management status, or -1 without an answer.
int MgmtSocket::command(uint16_t op, uint16_t controller, const Bytes& params, Bytes* reply) {
  if (fd_.get() < 0) return -1;
  const uint16_t index = op == kReadIndexList ? kNoDevice : controller;
  Bytes packet;
  put16(packet, op);
  put16(packet, index);
  put16(packet, static_cast<uint16_t>(params.size()));
  packet.insert(packet.end(), params.begin(), params.end());
  if (::write(fd_.get(), packet.data(), packet.size()) < 0) return -1;
  const int64_t deadline = posix::monotonicMs() + 2000;
  uint8_t buf[1024];
  while (posix::monotonicMs() < deadline) {
    pollfd p{fd_.get(), POLLIN, 0};
    if (::poll(&p, 1, 100) <= 0) continue;
    const ssize_t n = ::read(fd_.get(), buf, sizeof buf);
    if (n < 6) continue;
    const uint16_t event = le16(buf);
    if ((event == kCommandComplete || event == kCommandStatus) && n >= 9 && le16(buf + 6) == op) {
      const uint8_t status = buf[8];
      if (event == kCommandStatus && status == 0) continue;
      if (reply) reply->assign(buf + 9, buf + n);
      return status;
    }
    events_.emplace_back(buf, buf + n);
  }
  return -1;
}

void MgmtSocket::write(const Bytes& packet) const {
  (void)!::write(fd_.get(), packet.data(), packet.size());
}

void MgmtSocket::readEvents() {
  uint8_t buffer[1024];
  for (;;) {
    const ssize_t count = ::read(fd_.get(), buffer, sizeof buffer);
    if (count <= 0) break;
    events_.emplace_back(buffer, buffer + count);
  }
}

bool MgmtSocket::nextEvent(Bytes& event) {
  if (events_.empty()) return false;
  event = std::move(events_.front());
  events_.pop_front();
  return true;
}

}  // namespace awtrix::ble
