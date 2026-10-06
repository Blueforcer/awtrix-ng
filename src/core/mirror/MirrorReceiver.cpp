#include "core/mirror/MirrorReceiver.h"

#include <algorithm>
#include <cstring>
#include <new>

namespace awtrix {
namespace mirror {

Receiver::Receiver(net::IDatagramSink& sink, const net::Endpoint& source, Filter filter,
                   int width, int height)
    : sink_(sink), source_(source), filter_(std::move(filter)), width_(width), height_(height),
      frameBytes_(static_cast<std::size_t>(width) * height * 3u) {}

void Receiver::setFilter(Filter filter) { filter_ = std::move(filter); }

void Receiver::subscribe() {
  uint8_t packet[16];
  const std::size_t length = wire::encodeSubscribe(static_cast<uint16_t>(width_),
                                                   static_cast<uint16_t>(height_), packet,
                                                   sizeof(packet));
  sink_.send(source_, packet, length);
}

void Receiver::leave() {
  uint8_t packet[16];
  sink_.send(source_, packet, wire::encodeLeave(packet, sizeof(packet)));
}

void Receiver::tick(int64_t nowMs) {
  if (!subscribed_ || nowMs - lastSubscribeMs_ >= kSubscribeEveryMs) {
    subscribe();
    lastSubscribeMs_ = nowMs;
    subscribed_ = true;
  }
  if (heard_ && nowMs - lastHeardMs_ > kSilenceMs) {
    heard_ = false;
    assembling_ = false;
    state_ = FollowState::Waiting;
  }
}

bool Receiver::ensureBuffers() {
  if (!shown_) shown_.reset(new (std::nothrow) uint8_t[frameBytes_]);
  if (!building_) building_.reset(new (std::nothrow) uint8_t[frameBytes_]);
  if (!rowIn_) rowIn_.reset(new (std::nothrow) bool[height_]);
  return shown_ && building_ && rowIn_;
}

void Receiver::receive(const wire::Packet& packet, int64_t nowMs) {
  if (packet.type != wire::Type::Idle && packet.type != wire::Type::Frame) return;
  heard_ = true;
  lastHeardMs_ = nowMs;
  sourceWidth_ = packet.width;
  sourceHeight_ = packet.height;
  if (packet.width != width_ || packet.height != height_) {
    assembling_ = false;
    state_ = FollowState::SizeMismatch;
    return;
  }
  if (packet.type == wire::Type::Idle) {
    assembling_ = false;
    state_ = FollowState::Idle;
    return;
  }
  if (!filter_.admits(packet.kind, packet.app, packet.incoming)) {
    assembling_ = false;
    state_ = FollowState::Filtered;
    return;
  }
  if (!ensureBuffers()) {
    assembling_ = false;
    state_ = FollowState::NoMemory;
    return;
  }
  accept(packet);
}

void Receiver::accept(const wire::Packet& packet) {
  if (!assembling_ || packet.frame != buildingFrame_) {
    std::fill(rowIn_.get(), rowIn_.get() + height_, false);
    rowsIn_ = 0;
    buildingFrame_ = packet.frame;
    assembling_ = true;
  }
  const std::size_t rowBytes = static_cast<std::size_t>(width_) * 3u;
  std::memcpy(building_.get() + packet.firstRow * rowBytes, packet.rgb, packet.rows * rowBytes);
  for (int row = packet.firstRow; row < packet.firstRow + packet.rows; ++row) {
    if (!rowIn_[row]) {
      rowIn_[row] = true;
      ++rowsIn_;
    }
  }
  if (rowsIn_ < height_) return;
  std::swap(shown_, building_);
  assembling_ = false;
  state_ = FollowState::Showing;
}

void Receiver::draw(Canvas& canvas) const {
  if (!showing() || canvas.width() != width_ || canvas.height() != height_) return;
  const uint8_t* rgb = shown_.get();
  for (int y = 0; y < height_; ++y) {
    for (int x = 0; x < width_; ++x, rgb += 3)
      canvas.setPixel(x, y, (static_cast<uint32_t>(rgb[0]) << 16) |
                                (static_cast<uint32_t>(rgb[1]) << 8) | rgb[2]);
  }
}

}
}
