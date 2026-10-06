#include "core/mirror/Mirror.h"

#include <new>
#include <utility>

#include "core/mirror/MirrorWire.h"

namespace awtrix {
namespace mirror {

void Mirror::begin(int width, int height, Status& status) {
  if (width != width_ || height != height_) dropReceiver(online_);
  if (source_ && (width != width_ || height != height_)) {
    if (online_) source_->stop();
    source_.reset();
  }
  width_ = width;
  height_ = height;
  status_ = &status;
  applyShare();
  status_->source = config_.follow;
  updateStatus();
}

void Mirror::applyShare() {
  if (config_.share && width_ > 0 && height_ > 0) {
    Filter filter(config_.shareApps, config_.shareNotifications);
    if (source_) source_->setFilter(std::move(filter));
    else source_.reset(new (std::nothrow) Source(sink_, std::move(filter), width_, height_));
  } else if (source_) {
    if (online_) source_->stop();
    source_.reset();
  }
}

void Mirror::configure(const Config& config) {
  followFilter_ = Filter(config.followApps, config.followNotifications);
  if (config.follow != config_.follow) {
    dropReceiver(online_);
    lookup_ = FollowState::Resolving;
  } else if (receiver_) {
    receiver_->setFilter(followFilter_);
  }
  config_ = config;
  applyShare();
  status_->source = config_.follow;
  updateStatus();
}

void Mirror::setOnline(bool online) {
  if (online == online_) return;
  online_ = online;
  if (!online_) {
    if (source_) source_->drop();
    dropReceiver(false);
    lookup_ = FollowState::Resolving;
  }
  updateStatus();
}

void Mirror::setSource(const net::Endpoint& source) {
  if (config_.follow.empty() || !online_ || width_ <= 0 || height_ <= 0) return;
  if (receiver_ && receiver_->source() == source) return;
  dropReceiver(true);
  receiver_.reset(new (std::nothrow) Receiver(sink_, source, followFilter_, width_, height_));
  lookup_ = receiver_ ? FollowState::Waiting : FollowState::NoMemory;
  updateStatus();
}

void Mirror::setLookup(FollowState state) {
  dropReceiver(online_);
  lookup_ = state;
  updateStatus();
}

void Mirror::receive(const net::Endpoint& from, const uint8_t* data, std::size_t length,
                     int64_t nowMs) {
  if (!online_) return;
  wire::Packet packet;
  if (!wire::decode(data, length, packet)) return;
  switch (packet.type) {
    case wire::Type::Subscribe:
      if (source_) source_->subscribe(from, packet.width, packet.height, nowMs);
      break;
    case wire::Type::Leave:
      if (source_) source_->leave(from);
      break;
    case wire::Type::Idle:
    case wire::Type::Frame:
      if (receiver_ && from == receiver_->source()) receiver_->receive(packet, nowMs);
      break;
  }
  updateStatus();
}

void Mirror::tick(int64_t nowMs) {
  if (online_) {
    if (source_) source_->expire(nowMs);
    if (receiver_) receiver_->tick(nowMs);
  }
  updateStatus();
}

void Mirror::draw(Canvas& canvas) const {
  if (receiver_) receiver_->draw(canvas);
}

void Mirror::content(const Canvas& frame, const PageInfo* page, int64_t nowMs) {
  if (!online_ || !source_) return;
  source_->publish(frame, page, nowMs);
  updateStatus();
}

void Mirror::dropReceiver(bool tellSource) {
  if (receiver_ && tellSource) receiver_->leave();
  receiver_.reset();
}

void Mirror::updateStatus() {
  Status& status = *status_;
  status.sharing = online_ && source_;
  status.viewers = source_ ? static_cast<uint8_t>(source_->viewers()) : 0;
  status.sourceWidth = receiver_ ? receiver_->sourceWidth() : 0;
  status.sourceHeight = receiver_ ? receiver_->sourceHeight() : 0;
  if (config_.follow.empty()) status.follow = FollowState::Off;
  else if (!online_) status.follow = FollowState::Offline;
  else if (receiver_) status.follow = receiver_->state();
  else status.follow = lookup_;
}

}
}
