#pragma once

#include "../../../support.h"

#include <cerrno>
#include <cstdio>
#include <linux/rtnetlink.h>
#include <map>
#include <string>
#include <vector>

#include "platform/tc002/daemon/ip/Ipv4.h"
#include "platform/tc002/daemon/ip/Netlink.h"

namespace ip_test {

using awtrix::test::failures;
using awtrix::test::check;
using awtrix::test::finish;

inline uint32_t ipv4(const char* text) {
  uint32_t out = 0;
  awtrix::tc002d::ip::parseIpv4(text, out);
  return out;
}

// Keeps addresses and main-table routes the way the kernel does for the operations we use:
// an address brings its prefix route, deleting it takes the prefix route along.
class FakeKernel : public awtrix::tc002d::ip::IpKernel {
 public:
  using KernelAddress = awtrix::tc002d::ip::KernelAddress;
  using KernelRoute = awtrix::tc002d::ip::KernelRoute;

  std::map<std::string, int> interfaces{{"lo", 1}, {"wlan0", 3}};
  std::vector<KernelAddress> addressTable;
  std::vector<KernelRoute> routeTable;
  std::vector<std::string> operations;
  int error = 0;
  int addAddressError = 0;

  int interfaceIndex(const std::string& name) override {
    const auto found = interfaces.find(name);
    error = found == interfaces.end() ? ENODEV : 0;
    return found == interfaces.end() ? 0 : found->second;
  }
  bool addresses(int ifindex, std::vector<KernelAddress>& out) override {
    out.clear();
    for (const KernelAddress& address : addressTable)
      if (!ifindex || address.ifindex == ifindex) out.push_back(address);
    return true;
  }
  bool routes(std::vector<KernelRoute>& out) override {
    out = routeTable;
    return true;
  }
  bool addAddress(const KernelAddress& address) override {
    operations.push_back("add-address " + awtrix::tc002d::ip::formatIpv4(address.address) + "/" +
                         std::to_string(address.prefix));
    if (addAddressError) return fail(addAddressError);
    if (!known(address.ifindex)) return fail(ENODEV);
    for (KernelAddress& existing : addressTable) {
      if (existing.ifindex == address.ifindex && existing.address == address.address &&
          existing.prefix == address.prefix) {
        existing = address;
        return true;
      }
    }
    addressTable.push_back(address);
    KernelRoute prefixRoute;
    prefixRoute.ifindex = address.ifindex;
    prefixRoute.prefix = address.prefix;
    prefixRoute.destination = address.address & awtrix::tc002d::ip::prefixMask(address.prefix);
    prefixRoute.table = RT_TABLE_MAIN;
    prefixRoute.protocol = RTPROT_KERNEL;
    routeTable.push_back(prefixRoute);
    error = 0;
    return true;
  }
  bool deleteAddress(const KernelAddress& address) override {
    operations.push_back("delete-address " + awtrix::tc002d::ip::formatIpv4(address.address));
    for (std::size_t i = 0; i < addressTable.size(); ++i) {
      if (addressTable[i].ifindex == address.ifindex && addressTable[i].address == address.address) {
        const KernelAddress removed = addressTable[i];
        addressTable.erase(addressTable.begin() + static_cast<std::ptrdiff_t>(i));
        for (std::size_t r = 0; r < routeTable.size();) {
          const KernelRoute& route = routeTable[r];
          if (route.protocol == RTPROT_KERNEL && route.ifindex == removed.ifindex && route.prefix == removed.prefix &&
              route.destination == (removed.address & awtrix::tc002d::ip::prefixMask(removed.prefix)))
            routeTable.erase(routeTable.begin() + static_cast<std::ptrdiff_t>(r));
          else
            ++r;
        }
        error = 0;
        return true;
      }
    }
    return fail(EADDRNOTAVAIL);
  }
  bool replaceDefaultRoute(int ifindex, uint32_t gateway, bool onlink) override {
    operations.push_back(std::string("replace-default ") + awtrix::tc002d::ip::formatIpv4(gateway) +
                         (onlink ? " onlink" : ""));
    if (!known(ifindex)) return fail(ENODEV);
    for (std::size_t i = 0; i < routeTable.size(); ++i) {
      if (routeTable[i].prefix == 0 && routeTable[i].metric == 0) {
        routeTable.erase(routeTable.begin() + static_cast<std::ptrdiff_t>(i));
        break;
      }
    }
    KernelRoute route;
    route.ifindex = ifindex;
    route.gateway = gateway;
    route.table = RT_TABLE_MAIN;
    route.protocol = RTPROT_DHCP;
    route.flags = onlink ? RTNH_F_ONLINK : 0;
    routeTable.push_back(route);
    error = 0;
    return true;
  }
  bool deleteRoute(const KernelRoute& route) override {
    operations.push_back("delete-route " + awtrix::tc002d::ip::formatIpv4(route.destination) + "/" +
                         std::to_string(route.prefix) + " via " + awtrix::tc002d::ip::formatIpv4(route.gateway));
    for (std::size_t i = 0; i < routeTable.size(); ++i) {
      const KernelRoute& existing = routeTable[i];
      if (existing.prefix == route.prefix && existing.destination == route.destination &&
          (!route.gateway || existing.gateway == route.gateway) && (!route.ifindex || existing.ifindex == route.ifindex) &&
          (!route.hasMetric || existing.metric == route.metric)) {
        routeTable.erase(routeTable.begin() + static_cast<std::ptrdiff_t>(i));
        error = 0;
        return true;
      }
    }
    return fail(ESRCH);
  }
  int lastError() const override { return error; }

  std::size_t addressesOn(int ifindex) const {
    std::size_t count = 0;
    for (const KernelAddress& address : addressTable) count += address.ifindex == ifindex;
    return count;
  }
  std::size_t defaultRoutesVia(uint32_t gateway) const {
    std::size_t count = 0;
    for (const KernelRoute& route : routeTable) count += route.prefix == 0 && route.gateway == gateway;
    return count;
  }
  std::size_t defaultRoutes() const {
    std::size_t count = 0;
    for (const KernelRoute& route : routeTable) count += route.prefix == 0;
    return count;
  }

 private:
  bool known(int ifindex) const {
    for (const auto& entry : interfaces)
      if (entry.second == ifindex) return true;
    return false;
  }
  bool fail(int number) {
    error = number;
    return false;
  }
};

}
