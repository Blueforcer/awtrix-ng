#include "transport/net/MirrorLink.h"

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
  if (next.follow == lookup_.host()) return;
  lookup_.setHost(next.follow);
  haveSource_ = false;
}

void MirrorLink::tick(int64_t nowMs, bool online) {
  if (online && mirror_.listening()) udp_.keepOpen(mirror::wire::kPort, nowMs);
  else if (udp_.close()) haveSource_ = false;
  mirror_.setOnline(udp_.isOpen());
  if (udp_.isOpen()) {
    net::Endpoint from;
    while (const std::size_t length = udp_.receive(from)) mirror_.receive(from, udp_.data(), length, nowMs);
    lookUp(nowMs);
  }
  mirror_.tick(nowMs);
}

// A name is looked up again after half a minute without an answer from the clock, in case it moved
// to another address; until then the known address is kept.
void MirrorLink::lookUp(int64_t nowMs) {
  switch (lookup_.poll(nowMs, haveSource_, mirror_.status().follow != mirror::FollowState::Waiting)) {
    case net::HostLookup::Result::Idle:
      return;
    case net::HostLookup::Result::Pending:
      if (!haveSource_) mirror_.setLookup(mirror::FollowState::Resolving);
      return;
    case net::HostLookup::Result::Failed:
      if (!haveSource_) mirror_.setLookup(mirror::FollowState::NotFound);
      return;
    case net::HostLookup::Result::Found:
      haveSource_ = true;
      mirror_.setSource({lookup_.address(), mirror::wire::kPort});
      return;
  }
}

}
