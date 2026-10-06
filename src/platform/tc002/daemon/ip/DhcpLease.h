#pragma once

#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

#include "platform/tc002/contract/SupervisorProtocol.h"

namespace awtrix {
namespace tc002d {
namespace ip {

constexpr uint32_t kInfiniteLease = UINT32_MAX;
constexpr uint32_t kDefaultLeaseSeconds = 3600;
constexpr std::size_t kMaxServers = 3;

enum class DhcpEvent : uint8_t { Deconfig, Bound, Renew, Nak, LeaseFail };

struct DhcpLease {
  uint32_t address = 0;
  uint8_t prefix = 0;
  uint32_t broadcast = 0;
  uint32_t router = 0;
  std::vector<uint32_t> dns;
  std::vector<uint32_t> ntp;
  uint32_t leaseSeconds = 0;
};

bool sameAddressing(const DhcpLease& a, const DhcpLease& b);
bool operator==(const DhcpLease& a, const DhcpLease& b);
inline bool operator!=(const DhcpLease& a, const DhcpLease& b) { return !(a == b); }

struct DhcpRecord {
  DhcpEvent event = DhcpEvent::Deconfig;
  int64_t capturedMs = -1;
  DhcpLease lease;
  std::string notes;
};

const char* dhcpEventName(DhcpEvent event);

// Parses one callback line (without its newline) and validates it for interfaceName.
// Bound/renew need a usable address and subnet; malformed optional fields are dropped
// and described in notes.
bool parseDhcpRecord(std::string_view line, std::string_view interfaceName, DhcpRecord& out,
                     std::string& error);

// A fixed address as an infinite lease, under the same rules as a DHCP one. An empty or 0.0.0.0
// gateway or DNS server means none; an empty dns1 falls back to the gateway.
bool staticLease(const tc002::StaticAddress& wanted, DhcpLease& out, std::string& error);
// A lease as a bound record that parseDhcpRecord reads back unchanged (without broadcast and ntp).
std::string formatLease(const DhcpLease& lease, std::string_view interfaceName);

}
}
}
