#include "transport/net/DiscoveryService.h"

#include <cstring>

#include "transport/net/ScriptHostLink.h"

namespace awtrix {

// Broadcast "FIND_AWTRIXNG" on 4210 and the device answers on 4211 with "host[:port]". Deliberately
// simple: it is the fallback for networks where mDNS does not get through. Script hosts also send
// their offers here; those are handed on.
namespace {
constexpr uint16_t kListenPort = 4210;
constexpr uint16_t kReplyPort = 4211;
constexpr std::size_t kDatagramBytes = 128;
}

void DiscoveryService::begin(const std::string& hostname, int webPort) {
  reply_ = hostname;
  const int p = webPort > 0 ? webPort : 80;
  if (p != 80) reply_ += ":" + std::to_string(p);
  active_ = udp_.open(kListenPort);
}

void DiscoveryService::tick() {
  if (!active_) return;
  char buf[kDatagramBytes];
  net::Endpoint from;
  const int n = udp_.receiveFrom(buf, sizeof(buf) - 1, from);
  if (n <= 0) return;
  if (n >= 4 && std::memcmp(buf, "AWRH", 4) == 0) {
    if (scriptHost_) scriptHost_->discovery(reinterpret_cast<const uint8_t*>(buf), static_cast<std::size_t>(n), from.address);
    return;
  }
  buf[n] = '\0';
  if (std::strncmp(buf, "FIND_AWTRIXNG", 13) == 0)
    udp_.sendTo({from.address, kReplyPort}, reply_.data(), reply_.size());
}

}
