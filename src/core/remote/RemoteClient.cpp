#include "core/remote/RemoteClient.h"

#include <cstring>
#include <utility>

#include "core/ConstantTime.h"

namespace awtrix {
namespace remote {

namespace {
bool asks(bool hostApp, const std::string& app, const std::string& asked) {
  return hostApp ? app == asked : asked.empty();
}

void ask(bool hostApp, const std::string& app, std::string& asked) {
  if (hostApp) asked = app;
  else asked.clear();
}
}

void RemoteClient::begin(std::string uid, int width, int height, uint16_t webPort, OwnAddress ownAddress,
                         wire::Signer sign, Random random) {
  uid_ = std::move(uid);
  width_ = static_cast<uint16_t>(width);
  height_ = static_cast<uint16_t>(height);
  webPort_ = webPort;
  ownAddress_ = ownAddress;
  sign_ = std::move(sign);
  helloCounter_ = random();
  settingsRevision_ = static_cast<uint16_t>(random());
  systemRevision_ = static_cast<uint16_t>(random());
  assetsRevision_ = static_cast<uint16_t>(random());
}

void RemoteClient::setHost(const net::Endpoint& host) {
  if (host == host_) return;
  host_ = host;
  greeted_ = false;
  showDirty_ = !current_.empty() || !incoming_.empty();
  setLink(false);
}

void RemoteClient::setState(uint8_t state, const wire::Sensors& sensors) {
  state_ = state;
  sensors_ = sensors;
}

void RemoteClient::setButtons(uint8_t down, const uint8_t (&presses)[wire::kButtons]) {
  if (down != down_ || std::memcmp(presses, presses_, sizeof(presses_)) != 0) showDirty_ = true;
  down_ = down;
  std::memcpy(presses_, presses, sizeof(presses_));
}

void RemoteClient::setLink(bool up) {
  linkUp_ = up;
  apps_.setLinkUp(up);
}

void RemoteClient::sendHello() {
  wire::Hello hello;
  hello.uid = uid_;
  hello.width = width_;
  hello.height = height_;
  hello.address = ownAddress_ ? ownAddress_(host_.address) : 0;
  hello.webPort = webPort_;
  hello.counter = ++helloCounter_;
  hello.settingsRevision = settingsRevision_;
  hello.systemRevision = systemRevision_;
  hello.assetsRevision = assetsRevision_;
  hello.appsRevision = greetedAppsRevision_ = apps_.revision();
  hello.offMask = greetedOffMask_ = apps_.offMask();
  hello.state = state_;
  hello.sensors = sensors_;
  uint8_t packet[128];
  const std::size_t length = wire::encodeHello(hello, sign_, packet, sizeof(packet));
  if (length) sink_.send(host_, packet, length);
}

void RemoteClient::sendShow() {
  wire::Show show;
  show.sequence = sequence_;
  show.buttonsDown = down_;
  std::memcpy(show.presses, presses_, sizeof(presses_));
  show.current = current_;
  show.incoming = incoming_;
  uint8_t packet[128];
  const std::size_t length = wire::encodeShow(show, packet, sizeof(packet));
  if (length) sink_.send(host_, packet, length);
}

void RemoteClient::tick(const std::string& current, const std::string& incoming, int64_t nowMs) {
  if (!hasHost()) return;
  if (linkUp_ && nowMs - lastHeardMs_ > kSilenceMs) setLink(false);
  const bool appsMoved = apps_.revision() != greetedAppsRevision_ || apps_.offMask() != greetedOffMask_;
  if (!greeted_ || appsMoved || nowMs - lastHelloMs_ >= kHelloEveryMs) {
    sendHello();
    greeted_ = true;
    lastHelloMs_ = nowMs;
  }
  const bool hostCurrent = apps_.owns(current);
  const bool hostIncoming = apps_.owns(incoming);
  if (!asks(hostCurrent, current, current_) || !asks(hostIncoming, incoming, incoming_)) {
    ask(hostCurrent, current, current_);
    ask(hostIncoming, incoming, incoming_);
    frames_.want(current_, incoming_);
    showDirty_ = true;
  }
  const bool asking = !current_.empty() || !incoming_.empty();
  if (showDirty_) {
    ++sequence_;
    sendShow();
    showDirty_ = false;
    lastShowMs_ = nowMs;
  } else if (asking && nowMs - lastShowMs_ >= kShowEveryMs) {
    sendShow();
    lastShowMs_ = nowMs;
  }
  frames_.play(nowMs);
}

void RemoteClient::receive(const net::Endpoint& from, const uint8_t* data, std::size_t length,
                           int64_t nowMs) {
  if (!hasHost() || from != host_) return;
  wire::Packet packet;
  if (!wire::decode(data, length, packet)) return;
  switch (packet.type) {
    case wire::Type::Apps:
      apps_.apply(packet.apps);
      break;
    case wire::Type::Frame:
      frames_.receive(packet.frame, nowMs);
      break;
    default:
      return;
  }
  lastHeardMs_ = nowMs;
  if (!linkUp_) setLink(true);
}

bool RemoteClient::follow(const wire::Packet& offer, uint32_t fromAddress, int64_t unixNow) {
  if (linkUp_ || offer.type != wire::Type::Offer || !offer.tag || !sign_ || offer.offer.uid != uid_) return false;
  const int64_t sent = offer.offer.unixTime;
  if (unixNow < kKnownUnixTime || sent - unixNow > kOfferSkewS || unixNow - sent > kOfferSkewS) return false;
  if (offer.offer.unixTime <= offerTime_) return false;
  uint8_t tag[wire::kTagBytes];
  sign_(offer.tag - offer.signedBytes, offer.signedBytes, tag);
  if (!constantTimeEqual(tag, offer.tag, wire::kTagBytes)) return false;
  offerTime_ = offer.offer.unixTime;
  const net::Endpoint host{fromAddress, offer.offer.port};
  if (host == host_) return false;
  setHost(host);
  return true;
}

}
}
