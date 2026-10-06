#include "platform/linux/ble/LinuxBleRadio.h"

#include <algorithm>
#include <cerrno>
#include <cstring>
#include <fcntl.h>
#include <sys/socket.h>
#include <unistd.h>

#include "platform/linux/ble/LinuxBleProtocol.h"

namespace awtrix::ble {
using namespace linux_detail;

bool LinuxBleRadio::openScanner() {
  scanFd_ = ::socket(kAfBluetooth, SOCK_RAW | SOCK_CLOEXEC | SOCK_NONBLOCK, kProtoHci);
  if (scanFd_ < 0) return false;
  SockaddrHci a{static_cast<sa_family_t>(kAfBluetooth), index_, kChannelRaw};
  HciFilter f{};
  f.typeMask = 1u << 4;
  f.eventMask[0] = (1u << 0x05) | (1u << 0x0e) | (1u << 0x0f);
  f.eventMask[1] = 1u << (0x3e - 32);
  if (::bind(scanFd_, reinterpret_cast<sockaddr*>(&a), sizeof a) < 0 ||
      ::setsockopt(scanFd_, kSolHci, kHciFilter, &f, sizeof f) < 0) {
    ::close(scanFd_);
    scanFd_ = -1;
    return false;
  }
  return true;
}

bool LinuxBleRadio::hciCommand(uint16_t op, const Bytes& params) {
  if (scanFd_ < 0) return false;
  Bytes packet{0x01};
  put16(packet, op);
  packet.push_back(static_cast<uint8_t>(params.size()));
  packet.insert(packet.end(), params.begin(), params.end());
  return ::write(scanFd_, packet.data(), packet.size()) == static_cast<ssize_t>(packet.size());
}

bool LinuxBleRadio::scan(bool on, bool active, bool background) {
  scanWanted_ = on;
  scanActive_ = active;
  scanBackground_ = background;
  applyScan();
  return scanFd_ >= 0 || !on;
}

bool LinuxBleRadio::connecting() const {
  if (cancel_) return true;
  for (const auto& [id, link] : links_)
    if (link.outgoing && !link.open && (link.fd >= 0 || id == directLink_)) return true;
  return false;
}

bool LinuxBleRadio::incoming() const {
  return std::any_of(links_.begin(), links_.end(), [](const auto& e) { return e.second.peripheral && e.second.open; });
}

void LinuxBleRadio::restoreAdvertising() {
  if (adverts_.empty() || incoming() || connecting()) return;
  // Linux 4.9 skips even the advertising parameters once a LE link exists.
  if (legacyKernel_ && !links_.empty()) {
    const bool connectable = std::any_of(adverts_.begin(), adverts_.end(), [](const auto& e) { return e.second.connectable; });
    const bool scannable = std::any_of(adverts_.begin(), adverts_.end(), [](const auto& e) { return e.second.scannable; });
    Bytes params;
    put16(params, kAdvertisingInterval);
    put16(params, kAdvertisingInterval);
    params.insert(params.end(), {static_cast<uint8_t>(connectable ? 0 : scannable ? 2 : 3), 0, 0, 0, 0, 0, 0, 0, 0, 7, 0});
    hciCommand(0x200a, {0x00});
    hciCommand(0x2006, params);
  }
  hciCommand(0x200a, {0x01});
}

// Scanning and advertising wait while a connection is set up or undone; this brings back what is
// wanted once neither is going on.
void LinuxBleRadio::resume() {
  if (scanWanted_) applyScan();
  restoreAdvertising();
}

// Scan 30 ms every 100 ms (background: every 640 ms); untouched while a connection starts,
// then set again after all connects finish.
void LinuxBleRadio::applyScan() {
  if (scanFd_ < 0 || connecting()) return;
  hciCommand(0x200c, {0x00, 0x00});
  if (!scanWanted_) return;
  const uint8_t interval = scanBackground_ ? 0x04 : 0x00;
  hciCommand(0x200b, {static_cast<uint8_t>(scanActive_ ? 1 : 0), static_cast<uint8_t>(scanBackground_ ? 0x00 : 0xa0),
                      interval, 0x30, 0x00, 0x00, 0x00});
  hciCommand(0x200c, {0x01, 0x00});
  lastReport_ = now_;
}

void LinuxBleRadio::readScanner() {
  uint8_t buf[300];
  for (;;) {
    const ssize_t n = ::read(scanFd_, buf, sizeof buf);
    if (n <= 0) return;
    hciEvent(buf, static_cast<std::size_t>(n));
    if (n < 5 || buf[0] != 0x04 || buf[1] != 0x3e || buf[3] != 0x02) continue;
    lastReport_ = now_;
    const uint8_t count = buf[4];
    std::size_t i = 5;
    for (uint8_t k = 0; k < count && i + 9 <= static_cast<std::size_t>(n); ++k) {
      AdvReport r;
      r.eventType = buf[i];
      r.addr = fromWire(buf + i + 2, buf[i + 1] == 0 ? kLePublic : kLeRandom);
      const std::size_t len = buf[i + 8];
      if (i + 9 + len + 1 > static_cast<std::size_t>(n)) break;
      r.data.assign(buf + i + 9, buf + i + 9 + len);
      r.rssi = static_cast<int8_t>(buf[i + 9 + len]);
      i += 10 + len;
      if (events_.advert) events_.advert(r);
    }
  }
}

void LinuxBleRadio::hciEvent(const uint8_t* p, std::size_t n) {
  if (n < 3 || p[0] != 0x04 || n < static_cast<std::size_t>(p[2]) + 3) return;
  if (p[1] == 0x05 && p[2] >= 4 && p[3] == 0) {
    const uint16_t handle = le16(p + 4);
    peripheralHandles_.erase(handle);
    if (directLink_) {
      auto it = links_.find(directLink_);
      if (it != links_.end() && it->second.handle == handle) failDirect("the connection closed before ATT setup");
    }
    if (cancel_ && cancel_->handle == handle) finishCancel();
    return;
  }
  uint16_t op = 0;
  uint8_t status = 0;
  if (p[1] == 0x0e && p[2] >= 4) {
    op = le16(p + 4);
    status = p[6];
  } else if (p[1] == 0x0f && p[2] >= 4) {
    op = le16(p + 5);
    status = p[3];
  }
  if (op == 0x0406 || op == 0x2006 || (op >= 0x200a && op <= 0x200e)) {
    if (status) {
      if (!hciErrors_.count(op) || hciErrors_[op] != status) {
        char text[80];
        std::snprintf(text, sizeof text, "ble: HCI 0x%04x refused (status 0x%02x)", op, status);
        note(text);
      }
      hciErrors_[op] = status;
    } else {
      hciErrors_.erase(op);
    }
    if (op == 0x200d && status && directLink_) failDirect("the controller refused the connection (status " + std::to_string(status) + ")");
    // Unknown Connection Identifier: the link being disconnected is gone already. Only a cancel
    // with a handle sent a Disconnect; without one the answer is to someone else's.
    if (op == 0x0406 && status == 0x02 && cancel_ && cancel_->handle) finishCancel();
    return;
  }
  if (p[1] != 0x3e || p[2] < 19 || (p[3] != 0x01 && p[3] != 0x0a)) return;
  status = p[4];
  if (!status) peripheralHandles_[le16(p + 5)] = p[7] != 0;
  if (!status && p[7] != 0) return;
  if (cancel_) {
    // A cancelled initiation ends with a failure; one that won the race made a link to undo.
    if (status) {
      finishCancel();
    } else {
      cancel_->handle = le16(p + 5);
      if (!sendDisconnect(*cancel_->handle)) finishCancel();
    }
    return;
  }
  if (!directLink_) return;
  auto it = links_.find(directLink_);
  if (it == links_.end()) return;
  if (status) {
    failDirect("connection failed (status " + std::to_string(status) + ")");
  } else if (std::equal(it->second.peer.b.begin(), it->second.peer.b.end(), p + 9) ||
             (p[3] == 0x0a && p[2] >= 31 && std::equal(it->second.peer.b.begin(), it->second.peer.b.end(), p + 21))) {
    it->second.handle = le16(p + 5);
  }
}

}  // namespace awtrix::ble
