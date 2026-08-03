#include "transport/net/DiscoveryService.h"

#include <cstring>

namespace awtrix {

// Broadcast "FIND_AWTRIXNG" on 4210 and the device answers on 4211 with "host[:port]". Deliberately
// simple: it is the fallback for networks where mDNS does not get through.
namespace {
constexpr uint16_t kListenPort = 4210;
constexpr uint16_t kReplyPort = 4211;
constexpr std::size_t kQueryBytes = 32;
}

void DiscoveryService::begin(const std::string& hostname, int webPort) {
  reply_ = hostname;
  const int p = webPort > 0 ? webPort : 80;
  if (p != 80) reply_ += ":" + std::to_string(p);
  active_ = udp_.open(kListenPort);
}

void DiscoveryService::tick() {
  if (!active_) return;
  char buf[kQueryBytes];
  const int n = udp_.receive(buf, sizeof(buf) - 1);
  if (n <= 0) return;
  buf[n] = '\0';
  if (std::strncmp(buf, "FIND_AWTRIXNG", 13) == 0)
    udp_.replyTo(kReplyPort, reply_.data(), reply_.size());
}

}
