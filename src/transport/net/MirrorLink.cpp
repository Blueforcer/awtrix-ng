#include "transport/net/MirrorLink.h"

#include <new>

#include "core/mirror/MirrorWire.h"

namespace awtrix {

namespace {
std::string trimmed(const std::string& text) {
  const std::size_t first = text.find_first_not_of(" \t");
  if (first == std::string::npos) return std::string();
  return text.substr(first, text.find_last_not_of(" \t") - first + 1);
}
}

void MirrorLink::configure(const DeviceConfig& config) {
  mirror::Config next;
  next.share = config.mirrorShare;
  next.shareApps = config.mirrorShareApps;
  next.shareNotifications = config.mirrorShareNotifications;
  next.follow = trimmed(config.mirrorFrom);
  next.followApps = config.mirrorFromApps;
  next.followNotifications = config.mirrorFromNotifications;
  mirror_.configure(next);
  if (next.follow == host_) return;
  host_ = next.follow;
  haveSource_ = false;
  relooking_ = false;
  lookupAtMs_ = 0;
  if (resolver_) resolver_->forget();
}

bool MirrorLink::send(const net::Endpoint& to, const uint8_t* data, std::size_t length) {
  return socket_.sendTo(to, data, length);
}

void MirrorLink::tick(int64_t nowMs, bool online) {
  const bool wanted = online && mirror_.listening();
  if (wanted && !socket_.isOpen() && nowMs >= openRetryAtMs_) {
    if (!rx_) rx_.reset(new (std::nothrow) uint8_t[mirror::wire::kMaxDatagram + 1]);
    if (!rx_ || !socket_.open(mirror::wire::kPort)) openRetryAtMs_ = nowMs + kOpenRetryMs;
  }
  if (!wanted && socket_.isOpen()) {
    socket_.close();
    rx_.reset();
    haveSource_ = false;
    relooking_ = false;
  }
  mirror_.setOnline(socket_.isOpen());
  if (socket_.isOpen()) {
    drain(nowMs);
    lookUp(nowMs);
  }
  mirror_.tick(nowMs);
}

void MirrorLink::drain(int64_t nowMs) {
  constexpr std::size_t kCapacity = mirror::wire::kMaxDatagram + 1;
  net::Endpoint from;
  int length;
  while ((length = socket_.receiveFrom(rx_.get(), kCapacity, from)) > 0) {
    if (static_cast<std::size_t>(length) < kCapacity)
      mirror_.receive(from, rx_.get(), static_cast<std::size_t>(length), nowMs);
  }
}

// A name is looked up again after half a minute without an answer from the clock, in case it moved
// to another address; until then the known address is kept.
void MirrorLink::lookUp(int64_t nowMs) {
  if (host_.empty() || nowMs < lookupAtMs_) return;
  if (haveSource_ && !relooking_) {
    if (mirror_.status().follow != mirror::FollowState::Waiting ||
        nowMs - resolvedAtMs_ < kRelookupMs)
      return;
    if (resolver_) resolver_->forget();
    relooking_ = true;
  }
  if (!resolver_) resolver_ = net::makeHostResolver();
  switch (resolver_->resolve(host_)) {
    case net::ResolveState::Pending:
      if (!haveSource_) mirror_.setLookup(mirror::FollowState::Resolving);
      return;
    case net::ResolveState::Failed:
      resolver_->forget();
      relooking_ = false;
      resolvedAtMs_ = nowMs;
      lookupAtMs_ = nowMs + kLookupRetryMs;
      if (!haveSource_) mirror_.setLookup(mirror::FollowState::NotFound);
      return;
    case net::ResolveState::Ready:
      break;
  }
  source_.address = resolver_->address();
  source_.port = mirror::wire::kPort;
  haveSource_ = true;
  relooking_ = false;
  resolvedAtMs_ = nowMs;
  mirror_.setSource(source_);
}

}
