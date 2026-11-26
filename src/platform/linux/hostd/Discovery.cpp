#include "platform/linux/hostd/Discovery.h"

#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>

#include <ctime>

#include "core/remote/PairingKey.h"
#include "core/remote/RemoteWire.h"
#include "platform/linux/hostd/ClockStore.h"
#include "transport/net/HmacSha256.h"

namespace awtrix::hostd {

namespace {
constexpr uint16_t kClockPort = 4210;
constexpr uint16_t kReplyPort = 4211;
}

Discovery::~Discovery() {
  if (fd_ >= 0) ::close(fd_);
}

bool Discovery::begin(std::string name, uint16_t port) {
  name_ = std::move(name);
  if (name_.size() > remote::wire::kMaxName) name_.resize(remote::wire::kMaxName);
  port_ = port;
  fd_ = ::socket(AF_INET, SOCK_DGRAM | SOCK_NONBLOCK | SOCK_CLOEXEC, 0);
  if (fd_ < 0) return false;
  const int on = 1;
  ::setsockopt(fd_, SOL_SOCKET, SO_REUSEADDR, &on, sizeof(on));
  ::setsockopt(fd_, SOL_SOCKET, SO_BROADCAST, &on, sizeof(on));
  sockaddr_in local{};
  local.sin_family = AF_INET;
  local.sin_addr.s_addr = htonl(INADDR_ANY);
  local.sin_port = htons(kReplyPort);
  if (::bind(fd_, reinterpret_cast<sockaddr*>(&local), sizeof(local)) < 0) {
    ::close(fd_);
    fd_ = -1;
    return false;
  }
  return true;
}

void Discovery::send(const uint8_t* packet, std::size_t length, uint32_t address) {
  sockaddr_in to{};
  to.sin_family = AF_INET;
  to.sin_port = htons(kClockPort);
  to.sin_addr.s_addr = address;
  ::sendto(fd_, packet, length, 0, reinterpret_cast<sockaddr*>(&to), sizeof(to));
}

void Discovery::ask(const ClockStore& store) {
  static const char kQuery[] = "FIND_AWTRIXNG";
  const in_addr_t everyone[] = {htonl(INADDR_LOOPBACK), htonl(INADDR_BROADCAST)};
  for (const in_addr_t address : everyone)
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
      for (const in_addr_t address : everyone) send(packet, length, address);
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

void Discovery::tick(int64_t nowMs, const ClockStore& store) {
  if (fd_ < 0) return;
  if (nowMs >= nextAskMs_) {
    ask(store);
    nextAskMs_ = nowMs + kEveryMs;
  }
  char reply[128];
  sockaddr_in from{};
  socklen_t fromLength = sizeof(from);
  while (::recvfrom(fd_, reply, sizeof(reply), 0, reinterpret_cast<sockaddr*>(&from), &fromLength) > 0) {
    offer(from.sin_addr.s_addr);
    fromLength = sizeof(from);
  }
}

}
