#include "platform/posix/Bytes.h"
#include "core/payload/Crc.h"
#include "platform/tc002/daemon/McuUpgrade.h"

#include <algorithm>
#include <cerrno>
#include <unistd.h>

namespace awtrix::tc002d::mcu {
namespace {
const std::vector<uint8_t> handshake{0xcc, 0xaa, 0x55, 0xee, 0x12, 0x19, 0xe4};

}

uint32_t Upgrade::crc(const uint8_t* data, std::size_t size, bool file) {
  const uint32_t value = crc32Update(0xffffffffu, data, size);
  return file ? value ^ 0xffffffff : value;
}

void Upgrade::start(const std::vector<uint8_t>& image, int64_t now) {
  *this = Upgrade{};
  image_ = &image;
  if (image.empty() || image.size() > 262144) return fail("invalid image size");
  totalExpires_ = now + kLimitMs;
  phase_ = Phase::Handshake;
  queue(handshake, now);
}

void Upgrade::queue(std::vector<uint8_t> bytes, int64_t now) {
  posix::put32(bytes, crc(bytes.data(), bytes.size()));
  tx_ = std::move(bytes);
  sent_ = 0;
  expires_ = now + 3000;
}

void Upgrade::fail(const std::string& error) {
  phase_ = Phase::Failed;
  error_ = error;
  tx_.clear();
  sent_ = 0;
}

int64_t Upgrade::deadline() const { return active() ? std::min(expires_, totalExpires_) : -1; }
void Upgrade::tick(int64_t now) {
  if (active() && now >= deadline()) fail("MCU transfer timeout; automatic retry blocked");
}

void Upgrade::packet(int64_t now) {
  std::vector<uint8_t> bytes{static_cast<uint8_t>(blockSent_ ? 0x20 : 0xa0), txSeq_++};
  if (!blockSent_) {
    posix::put32(bytes, static_cast<uint32_t>(offset_));
    posix::put32(bytes, static_cast<uint32_t>(blockBytes_));
  }
  const std::size_t count = std::min(packet_, blockBytes_ - blockSent_);
  const auto first = image_->begin() + static_cast<std::ptrdiff_t>(offset_ + blockSent_);
  bytes.insert(bytes.end(), first, first + static_cast<std::ptrdiff_t>(count));
  blockSent_ += count;
  queue(std::move(bytes), now);
}

void Upgrade::flush(int fd, int64_t now) {
  tick(now);
  for (unsigned round = 0; active() && pending() && round < 4; ++round) {
    const ssize_t n = ::write(fd, tx_.data() + sent_, tx_.size() - sent_);
    if (n < 0 && errno == EINTR) continue;
    if (n < 0 && (errno == EAGAIN || errno == EWOULDBLOCK)) return;
    if (n <= 0) return fail("MCU UART write failed");
    sent_ += static_cast<std::size_t>(n);
    if (!pending()) {
      expires_ = now + (phase_ == Phase::Data ? 15000 : 5000);
      if (phase_ == Phase::Data && blockSent_ < blockBytes_) packet(now);
    }
  }
}

void Upgrade::receive(const uint8_t* data, std::size_t size, int64_t now) {
  tick(now);
  if (!active()) return;
  if (rx_.size() + size > 8192) return fail("MCU reply buffer overflow");
  rx_.insert(rx_.end(), data, data + size);
  while (active() && !rx_.empty()) {
    const uint8_t tag = rx_[0];
    std::size_t n = 0;
    if (tag == 0xcc) n = 11;
    else if (tag == 0x90) n = 7;
    else if (tag == 0x92 || tag == 0xff) {
      if (rx_.size() < 4) return;
      if (tag == 0xff && (rx_[1] != 0x55 || rx_[3] > 32)) { rx_.erase(rx_.begin()); continue; }
      n = rx_[3] + (tag == 0xff ? 6u : 8u);
    } else { rx_.erase(rx_.begin()); continue; }
    if (rx_.size() < n) return;
    std::vector<uint8_t> bytes(rx_.begin(), rx_.begin() + static_cast<std::ptrdiff_t>(n));
    rx_.erase(rx_.begin(), rx_.begin() + static_cast<std::ptrdiff_t>(n));
    if (tag == 0xff) continue; // In-flight normal reports preceding upgrade mode.
    if (crc(bytes.data(), n-4) != posix::le32(bytes.data()+n-4)) return fail("MCU reply CRC mismatch");
    if (pending()) return fail("MCU reply arrived before request completed");
    reply(bytes, now);
  }
}

void Upgrade::reply(const std::vector<uint8_t>& b, int64_t now) {
  if (phase_ == Phase::Handshake) {
    if (b.size() != 11 || !std::equal(handshake.begin(), handshake.end(), b.begin()))
      return fail("unexpected MCU handshake");
    phase_ = Phase::Config;
    std::vector<uint8_t> request{0x92, 0, 2, 10, 0x7b, 0};
    posix::put32(request, crc(image_->data(), image_->size(), true));
    posix::put32(request, static_cast<uint32_t>(image_->size()));
    queue(std::move(request), now);
  } else if (phase_ == Phase::Config) {
    if (b.size() != 19 || b[0] != 0x92 || b[2] != 2 || b[3] != 11) return fail("unexpected MCU configuration");
    block_ = posix::le32(b.data()+8);
    packet_ = b[12] | unsigned(b[13]) << 8;
    if (b[14] != 1) return fail("MCU declined update (enabled != 1)");
    if (posix::le32(b.data()+4) != 0) return fail("MCU requested resume; automatic resume unsupported");
    if (!block_ || block_ > 65536 || !packet_ || packet_ > 4096) return fail("unsupported MCU transfer geometry");
    rxSeq_ = static_cast<uint8_t>(b[1] + 1);
    blockBytes_ = std::min(block_, image_->size());
    phase_ = Phase::Data;
    attempts_ = 1;
    packet(now);
  } else if (phase_ == Phase::Data) {
    if (b.size() != 7 || b[0] != 0x90 || b[1] != rxSeq_++ || blockSent_ != blockBytes_)
      return fail("unexpected MCU block acknowledgement");
    if (b[2] == 0 || b[2] == 0xff) {
      offset_ += blockBytes_;
      if (offset_ == image_->size()) { phase_ = Phase::Done; return; }
      blockBytes_ = std::min(block_, image_->size() - offset_);
      attempts_ = 1;
    } else if (b[2] == 0xf7 || b[2] == 0xfa || attempts_++ >= 2) {
      return fail("MCU rejected flash block: " + std::to_string(b[2]));
    }
    blockSent_ = 0;
    packet(now);
  }
}
}
