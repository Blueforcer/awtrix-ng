#pragma once

#include <string>

#include "platform/tc002/daemon/ip/DhcpLease.h"
#include "platform/tc002/daemon/ip/Netlink.h"

namespace awtrix {
namespace tc002d {
namespace ip {

// Puts a DHCP lease on one interface: address with prefix and broadcast plus the default route.
// Remembers exactly what it added so that remove() takes away nothing else.
class LeaseApplier {
 public:
  LeaseApplier(IpKernel& kernel, std::string interfaceName)
      : kernel_(kernel), interface_(std::move(interfaceName)) {}

  // Clears addresses and non-kernel main-table routes that another DHCP client left behind.
  bool flush(std::string& error);
  bool apply(const DhcpLease& lease, std::string& error);
  bool remove(std::string& error);

  bool applied() const { return applied_; }
  const DhcpLease& lease() const { return lease_; }
  int ifindex() const { return ifindex_; }

 private:
  bool deleteRoute(uint32_t gateway, std::string& error);

  IpKernel& kernel_;
  std::string interface_;
  bool applied_ = false;
  bool routeApplied_ = false;
  int ifindex_ = 0;
  DhcpLease lease_;
};

}
}
}
