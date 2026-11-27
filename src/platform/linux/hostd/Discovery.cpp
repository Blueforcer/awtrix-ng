#include "platform/linux/hostd/Discovery.h"

#include <algorithm>
#include <cstdio>
#include <ctime>

#include "core/remote/HostAddress.h"
#include "core/remote/PairingKey.h"
#include "core/remote/RemoteWire.h"
#include "platform/linux/hostd/ClockStore.h"
#include "transport/net/HmacSha256.h"

namespace awtrix::hostd {

Discovery::Discovery(uint16_t clockPort, uint16_t replyPort) : clockPort_(clockPort), replyPort_(replyPort) {}

Discovery::~Discovery() { os::closeSocket(socket_); }

bool Discovery::listen() {
  socket_ = os::openUdp(replyPort_, true);
  return socket_ != os::kNoSocket;
}

bool Discovery::begin(std::string name, uint16_t port) {
  name_ = std::move(name);
  if (name_.size() > remote::wire::kMaxName) name_.resize(remote::wire::kMaxName);
  port_ = port;
  started_ = true;
  if (listen()) return true;
  std::fprintf(stderr, "discovery: UDP port %u is taken, trying again every 30 s\n", replyPort_);
  return false;
}

void Discovery::send(const uint8_t* packet, std::size_t length, uint32_t address) {
  sockaddr_in to{};
  to.sin_family = AF_INET;
  to.sin_port = htons(clockPort_);
  to.sin_addr.s_addr = address;
  os::sendDatagram(socket_, packet, length, to);
}

void Discovery::ask(const ClockStore& store) {
  static const char kQuery[] = "FIND_AWTRIXNG";
  const uint32_t everyone[] = {htonl(INADDR_LOOPBACK), htonl(INADDR_BROADCAST)};
  for (const uint32_t address : everyone)
    send(reinterpret_cast<const uint8_t*>(kQuery), sizeof(kQuery) - 1, address);
  const uint32_t now = static_cast<uint32_t>(std::time(nullptr));
  for (const ClockRecord& clock : store.clocks()) {
    remote::PairingKey key;
    if (!remote::PairingKey::parse(clock.keyHex, key)) continue;
    remote::wire::Offer offer;
    offer.name = name_;
    offer.port = static_cast<uint16_t>(clock.port);
    offer.uid = clock.uid;
    offer.unixTime = now;
    uint8_t packet[remote::wire::kMaxDatagram];
    const std::size_t length =
        remote::wire::encodeOffer(offer, packet, sizeof(packet), remote::signerFor(key, hmacSha256));
    if (length)
      for (const uint32_t address : everyone) send(packet, length, address);
  }
}

void Discovery::offer(uint32_t address) {
  remote::wire::Offer offer;
  offer.name = name_;
  offer.port = port_;
  uint8_t packet[remote::wire::kMaxDatagram];
  const std::size_t length = remote::wire::encodeOffer(offer, packet, sizeof(packet));
  if (length) send(packet, length, address);
}

void Discovery::note(uint32_t address) {
  if (noted_.size() >= kMaxNoted || std::find(noted_.begin(), noted_.end(), address) != noted_.end()) return;
  noted_.push_back(address);
  std::printf("discovery: clock at %s answered\n", remote::addressText(ntohl(address)).c_str());
  std::fflush(stdout);
}

void Discovery::tick(int64_t nowMs, const ClockStore& store) {
  if (!started_) return;
  if (nowMs >= nextAskMs_) {
    nextAskMs_ = nowMs + kEveryMs;
    if (!listening()) {
      if (!listen()) return;
      std::printf("discovery: UDP port %u is free again\n", replyPort_);
      std::fflush(stdout);
    }
    ask(store);
  }
  if (!listening()) return;
  char reply[128];
  sockaddr_in from{};
  while (os::receiveDatagram(socket_, reply, sizeof(reply), from) > 0) {
    note(from.sin_addr.s_addr);
    offer(from.sin_addr.s_addr);
  }
}

}
