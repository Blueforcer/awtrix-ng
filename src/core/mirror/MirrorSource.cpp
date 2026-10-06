#include "core/mirror/MirrorSource.h"

#include <algorithm>
#include <new>

#include "core/mirror/MirrorWire.h"

namespace awtrix {
namespace mirror {

namespace {
constexpr uint32_t kFnvOffset = 2166136261u;
constexpr uint32_t kFnvPrime = 16777619u;

uint32_t mix(uint32_t hash, uint32_t value) {
  for (int shift = 0; shift < 32; shift += 8) {
    hash ^= (value >> shift) & 0xFFu;
    hash *= kFnvPrime;
  }
  return hash;
}

uint32_t mix(uint32_t hash, const std::string& text) {
  for (char c : text) hash = mix(hash, static_cast<uint8_t>(c));
  return mix(hash, static_cast<uint32_t>(text.size()));
}

uint32_t fingerprintOf(const Canvas& frame, const PageInfo& page) {
  uint32_t hash = mix(kFnvOffset, static_cast<uint32_t>(page.kind));
  hash = mix(hash, page.app);
  hash = mix(hash, page.incoming);
  const uint32_t* pixels = frame.data();
  for (std::size_t i = 0, n = frame.size(); i < n; ++i) hash = mix(hash, pixels[i]);
  return hash;
}
}

void Source::setFilter(Filter filter) {
  filter_ = std::move(filter);
  force_ = true;
}

bool Source::subscribe(const net::Endpoint& viewer, int width, int height, int64_t nowMs) {
  if (width != width_ || height != height_) {
    sendIdleTo(viewer);
    return false;
  }
  for (std::size_t i = 0; i < count_; ++i) {
    if (viewers_[i].endpoint == viewer) {
      viewers_[i].expiresMs = nowMs + kLeaseMs;
      return true;
    }
  }
  if (count_ == kMaxViewers) return false;
  viewers_[count_++] = {viewer, nowMs + kLeaseMs};
  force_ = true;
  return true;
}

void Source::leave(const net::Endpoint& viewer) {
  for (std::size_t i = 0; i < count_; ++i) {
    if (viewers_[i].endpoint == viewer) {
      viewers_[i] = viewers_[--count_];
      return;
    }
  }
}

void Source::expire(int64_t nowMs) {
  for (std::size_t i = 0; i < count_;) {
    if (nowMs >= viewers_[i].expiresMs) viewers_[i] = viewers_[--count_];
    else ++i;
  }
}

bool Source::ensureBuffer() {
  if (!buffer_) buffer_.reset(new (std::nothrow) uint8_t[wire::kMaxDatagram]);
  return buffer_ != nullptr;
}

void Source::sendAll(std::size_t length) {
  if (length == 0) return;
  for (std::size_t i = 0; i < count_; ++i) sink_.send(viewers_[i].endpoint, buffer_.get(), length);
}

void Source::sendFrame(const Canvas& frame, const PageInfo& page) {
  wire::FrameHeader header;
  header.frame = ++frameNumber_;
  header.kind = page.kind;
  header.app = page.app;
  header.incoming = page.incoming;
  const int step = wire::rowsPerDatagram(frame.width(), header);
  if (step <= 0) return;
  for (int row = 0; row < frame.height(); row += step) {
    const int rows = std::min(step, frame.height() - row);
    sendAll(wire::encodeFrame(frame, header, row, rows, buffer_.get(), wire::kMaxDatagram));
  }
}

void Source::sendIdle() {
  sendAll(wire::encodeIdle(width_, height_, buffer_.get(), wire::kMaxDatagram));
}

void Source::sendIdleTo(const net::Endpoint& viewer) {
  uint8_t packet[16];
  sink_.send(viewer, packet, wire::encodeIdle(width_, height_, packet, sizeof(packet)));
}

void Source::publish(const Canvas& frame, const PageInfo* page, int64_t nowMs) {
  expire(nowMs);
  if (count_ == 0) {
    force_ = true;
    return;
  }
  if (frame.width() != width_ || frame.height() != height_ || !ensureBuffer()) return;
  const bool share = page && filter_.admits(*page);
  const bool due = force_ || nowMs - lastSentMs_ >= kKeepaliveMs;
  if (share) {
    const uint32_t print = fingerprintOf(frame, *page);
    if (!due && shared_ && print == fingerprint_) return;
    sendFrame(frame, *page);
    fingerprint_ = print;
  } else {
    if (!due && !shared_) return;
    sendIdle();
  }
  shared_ = share;
  force_ = false;
  lastSentMs_ = nowMs;
}

void Source::stop() {
  if (count_ > 0 && ensureBuffer()) sendIdle();
  count_ = 0;
  force_ = true;
}

}
}
