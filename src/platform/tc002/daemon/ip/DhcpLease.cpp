#include "platform/tc002/daemon/ip/DhcpLease.h"

#include "platform/tc002/daemon/ip/Ipv4.h"

namespace awtrix {
namespace tc002d {
namespace ip {
namespace {

struct Field {
  std::string_view key, value;
};

bool splitFields(std::string_view line, std::vector<Field>& out, std::string& error) {
  out.clear();
  while (!line.empty()) {
    const std::size_t tab = line.find('\t');
    const std::string_view token = line.substr(0, tab);
    line = tab == std::string_view::npos ? std::string_view() : line.substr(tab + 1);
    const std::size_t equals = token.find('=');
    if (equals == std::string_view::npos || equals == 0) {
      error = "malformed field";
      return false;
    }
    const Field field{token.substr(0, equals), token.substr(equals + 1)};
    for (const Field& seen : out) {
      if (seen.key == field.key) {
        error = "duplicate field " + std::string(field.key);
        return false;
      }
    }
    out.push_back(field);
  }
  return true;
}

std::string_view lookup(const std::vector<Field>& fields, std::string_view key, bool& present) {
  for (const Field& field : fields) {
    if (field.key == key) {
      present = true;
      return field.value;
    }
  }
  present = false;
  return {};
}

bool parseDecimal(std::string_view text, std::size_t maxDigits, uint64_t& out) {
  if (text.empty() || text.size() > maxDigits) return false;
  uint64_t value = 0;
  for (char c : text) {
    if (c < '0' || c > '9') return false;
    value = value * 10 + static_cast<uint64_t>(c - '0');
  }
  out = value;
  return true;
}

void note(std::string& notes, const char* text) {
  if (!notes.empty()) notes += "; ";
  notes += text;
}

void parseServers(std::string_view list, std::vector<uint32_t>& out, std::string& notes,
                  const char* what) {
  bool dropped = false;
  while (!list.empty()) {
    const std::size_t space = list.find(' ');
    const std::string_view item = list.substr(0, space);
    list = space == std::string_view::npos ? std::string_view() : list.substr(space + 1);
    if (item.empty()) continue;
    uint32_t address = 0;
    if (!parseIpv4(item, address) || !isUnicast(address)) {
      dropped = true;
      continue;
    }
    bool duplicate = false;
    for (uint32_t known : out) duplicate = duplicate || known == address;
    if (!duplicate && out.size() < kMaxServers) out.push_back(address);
  }
  if (dropped) note(notes, what);
}

bool parseEvent(std::string_view name, DhcpEvent& out) {
  if (name == "deconfig") out = DhcpEvent::Deconfig;
  else if (name == "bound") out = DhcpEvent::Bound;
  else if (name == "renew") out = DhcpEvent::Renew;
  else if (name == "nak") out = DhcpEvent::Nak;
  else if (name == "leasefail") out = DhcpEvent::LeaseFail;
  else return false;
  return true;
}

bool hostAddressing(std::string_view ipText, std::string_view subnetText, DhcpLease& lease, std::string& error) {
  if (!parseIpv4(ipText, lease.address) || !isUnicast(lease.address)) {
    error = "invalid ip";
    return false;
  }
  uint32_t mask = 0;
  if (!parseIpv4(subnetText, mask) || !maskPrefix(mask, lease.prefix) || lease.prefix == 0) {
    error = "invalid subnet";
    return false;
  }
  if (lease.prefix <= 30) {
    const uint32_t host = lease.address & ~mask;
    if (host == 0 || host == ~mask) {
      error = "address is not a host in its subnet";
      return false;
    }
    lease.broadcast = lease.address | ~mask;
  }
  return true;
}

// Empty and 0.0.0.0 mean none.
bool optionalServer(const std::string& text, uint32_t& out) {
  out = 0;
  return text.empty() || (parseIpv4(text, out) && (out == 0 || isUnicast(out)));
}

bool parseLease(const std::vector<Field>& fields, DhcpLease& lease, std::string& notes,
                std::string& error) {
  bool present = false;
  if (!hostAddressing(lookup(fields, "ip", present), lookup(fields, "subnet", present), lease, error)) return false;
  const std::string_view broadcastText = lookup(fields, "broadcast", present);
  if (present) {
    uint32_t broadcast = 0;
    if (!parseIpv4(broadcastText, broadcast) || broadcast != lease.broadcast) note(notes, "broadcast ignored");
  }
  const std::string_view routerText = lookup(fields, "router", present);
  if (present) {
    std::vector<uint32_t> routers;
    std::string routerNotes;
    parseServers(routerText, routers, routerNotes, "router dropped");
    for (uint32_t router : routers) {
      if (router != lease.address) {
        lease.router = router;
        break;
      }
    }
    if (!lease.router) note(notes, "no usable router");
  }
  const std::string_view dnsText = lookup(fields, "dns", present);
  if (present) parseServers(dnsText, lease.dns, notes, "dns entry dropped");
  const std::string_view ntpText = lookup(fields, "ntpsrv", present);
  if (present) parseServers(ntpText, lease.ntp, notes, "ntp entry dropped");
  lease.leaseSeconds = kDefaultLeaseSeconds;
  const std::string_view leaseText = lookup(fields, "lease", present);
  uint64_t seconds = 0;
  if (present && parseDecimal(leaseText, 10, seconds) && seconds > 0 && seconds <= kInfiniteLease)
    lease.leaseSeconds = static_cast<uint32_t>(seconds);
  else note(notes, present ? "lease time invalid, assuming 1 h" : "no lease time, assuming 1 h");
  return true;
}

}

bool staticLease(const tc002::StaticAddress& wanted, DhcpLease& out, std::string& error) {
  DhcpLease lease;
  uint32_t dns1 = 0, dns2 = 0;
  if (!hostAddressing(wanted.ip, wanted.subnet, lease, error)) return false;
  if (!optionalServer(wanted.gateway, lease.router) || (lease.router && lease.router == lease.address)) {
    error = "invalid gateway";
    return false;
  }
  if (!optionalServer(wanted.dns1, dns1) || !optionalServer(wanted.dns2, dns2)) {
    error = "invalid dns";
    return false;
  }
  if (dns1 || lease.router) lease.dns.push_back(dns1 ? dns1 : lease.router);
  if (dns2) lease.dns.push_back(dns2);
  lease.leaseSeconds = kInfiniteLease;
  out = lease;
  return true;
}

std::string formatLease(const DhcpLease& lease, std::string_view interfaceName) {
  std::string out = "v=1\tevent=bound\tinterface=" + std::string(interfaceName) + "\tip=" +
                    formatIpv4(lease.address) + "\tsubnet=" + formatIpv4(prefixMask(lease.prefix));
  if (lease.router) out += "\trouter=" + formatIpv4(lease.router);
  if (!lease.dns.empty()) {
    out += "\tdns=";
    for (std::size_t i = 0; i < lease.dns.size(); ++i) out += (i ? " " : "") + formatIpv4(lease.dns[i]);
  }
  return out + "\tlease=" + std::to_string(lease.leaseSeconds);
}

bool sameAddressing(const DhcpLease& a, const DhcpLease& b) {
  return a.address == b.address && a.prefix == b.prefix && a.broadcast == b.broadcast &&
         a.router == b.router;
}

bool operator==(const DhcpLease& a, const DhcpLease& b) {
  return sameAddressing(a, b) && a.dns == b.dns && a.ntp == b.ntp && a.leaseSeconds == b.leaseSeconds;
}

const char* dhcpEventName(DhcpEvent event) {
  switch (event) {
    case DhcpEvent::Deconfig: return "deconfig";
    case DhcpEvent::Bound: return "bound";
    case DhcpEvent::Renew: return "renew";
    case DhcpEvent::Nak: return "nak";
    case DhcpEvent::LeaseFail: return "leasefail";
  }
  return "invalid";
}

bool parseDhcpRecord(std::string_view line, std::string_view interfaceName, DhcpRecord& out,
                     std::string& error) {
  out = DhcpRecord{};
  error.clear();
  std::vector<Field> fields;
  if (!splitFields(line, fields, error)) return false;
  bool present = false;
  if (lookup(fields, "v", present) != "1" || !present) {
    error = "unsupported record version";
    return false;
  }
  if (!parseEvent(lookup(fields, "event", present), out.event)) {
    error = "unknown event";
    return false;
  }
  if (lookup(fields, "interface", present) != interfaceName) {
    error = "foreign interface";
    return false;
  }
  const std::string_view mono = lookup(fields, "mono", present);
  uint64_t capturedMs = 0;
  if (present) {
    if (!parseDecimal(mono, 18, capturedMs)) {
      error = "invalid capture time";
      return false;
    }
    out.capturedMs = static_cast<int64_t>(capturedMs);
  }
  if (out.event != DhcpEvent::Bound && out.event != DhcpEvent::Renew) return true;
  return parseLease(fields, out.lease, out.notes, error);
}

}
}
}
