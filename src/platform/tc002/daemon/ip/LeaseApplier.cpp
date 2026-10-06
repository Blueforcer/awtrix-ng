#include "platform/tc002/daemon/ip/LeaseApplier.h"

#include <cerrno>
#include <cstring>
#include <linux/rtnetlink.h>

#include "platform/tc002/daemon/ip/Ipv4.h"

namespace awtrix {
namespace tc002d {
namespace ip {
namespace {

bool alreadyGone(int error) {
  return error == ESRCH || error == EADDRNOTAVAIL || error == ENODEV || error == ENOENT;
}

void describe(std::string& error, const char* what, int number) {
  if (error.empty()) error = std::string(what) + ": " + std::strerror(number ? number : EIO);
}

KernelAddress kernelAddress(int ifindex, const DhcpLease& lease) {
  KernelAddress address;
  address.ifindex = ifindex;
  address.address = lease.address;
  address.prefix = lease.prefix;
  address.broadcast = lease.broadcast;
  return address;
}

}

bool LeaseApplier::flush(std::string& error) {
  const int ifindex = kernel_.interfaceIndex(interface_);
  if (!ifindex) {
    describe(error, "interface lookup", kernel_.lastError());
    return false;
  }
  std::vector<KernelRoute> routes;
  std::vector<KernelAddress> addresses;
  if (!kernel_.routes(routes)) {
    describe(error, "route dump", kernel_.lastError());
    return false;
  }
  bool okay = true;
  for (const KernelRoute& route : routes) {
    if (route.ifindex != ifindex || route.protocol == RTPROT_KERNEL) continue;
    if (!kernel_.deleteRoute(route) && !alreadyGone(kernel_.lastError())) {
      describe(error, "stale route removal", kernel_.lastError());
      okay = false;
    }
  }
  if (!kernel_.addresses(ifindex, addresses)) {
    describe(error, "address dump", kernel_.lastError());
    return false;
  }
  for (const KernelAddress& address : addresses) {
    if (!kernel_.deleteAddress(address) && !alreadyGone(kernel_.lastError())) {
      describe(error, "stale address removal", kernel_.lastError());
      okay = false;
    }
  }
  applied_ = routeApplied_ = false;
  lease_ = DhcpLease{};
  return okay;
}

bool LeaseApplier::deleteRoute(uint32_t gateway, std::string& error) {
  routeApplied_ = false;
  KernelRoute route;
  route.ifindex = ifindex_;
  route.gateway = gateway;
  route.table = RT_TABLE_MAIN;
  if (kernel_.deleteRoute(route) || alreadyGone(kernel_.lastError())) return true;
  describe(error, "default route removal", kernel_.lastError());
  return false;
}

bool LeaseApplier::apply(const DhcpLease& lease, std::string& error) {
  const int ifindex = kernel_.interfaceIndex(interface_);
  if (!ifindex) {
    describe(error, "interface lookup", kernel_.lastError());
    return false;
  }
  if (applied_ && ifindex != ifindex_) applied_ = routeApplied_ = false;
  bool okay = true;
  if (applied_ && (lease_.address != lease.address || lease_.prefix != lease.prefix)) {
    if (routeApplied_) okay = deleteRoute(lease_.router, error) && okay;
    if (!kernel_.deleteAddress(kernelAddress(ifindex, lease_)) && !alreadyGone(kernel_.lastError())) {
      describe(error, "previous address removal", kernel_.lastError());
      okay = false;
    }
    applied_ = false;
  }
  if (!kernel_.addAddress(kernelAddress(ifindex, lease))) {
    describe(error, "address", kernel_.lastError());
    return false;
  }
  const uint32_t previousRouter = lease_.router;
  applied_ = true;
  ifindex_ = ifindex;
  lease_ = lease;
  if (lease.router) {
    const bool onlink = lease.prefix >= 31 || !sameSubnet(lease.router, lease.address, lease.prefix);
    routeApplied_ = kernel_.replaceDefaultRoute(ifindex, lease.router, onlink);
    if (!routeApplied_) {
      describe(error, "default route", kernel_.lastError());
      okay = false;
    }
  } else if (routeApplied_) {
    okay = deleteRoute(previousRouter, error) && okay;
  }
  return okay;
}

bool LeaseApplier::remove(std::string& error) {
  if (!applied_) return true;
  bool okay = !routeApplied_ || deleteRoute(lease_.router, error);
  if (!kernel_.deleteAddress(kernelAddress(ifindex_, lease_)) && !alreadyGone(kernel_.lastError())) {
    describe(error, "address removal", kernel_.lastError());
    okay = false;
  }
  applied_ = false;
  lease_ = DhcpLease{};
  return okay;
}

}
}
}
