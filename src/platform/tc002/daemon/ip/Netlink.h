#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace awtrix {
namespace tc002d {
namespace ip {

struct KernelAddress {
  int ifindex = 0;
  uint32_t address = 0;
  uint8_t prefix = 0;
  uint32_t broadcast = 0;
  uint8_t flags = 0;
};

struct KernelRoute {
  int ifindex = 0;
  uint32_t destination = 0;
  uint8_t prefix = 0;
  uint32_t gateway = 0;
  uint32_t table = 0;
  uint32_t metric = 0;
  uint8_t protocol = 0;
  uint8_t scope = 0;
  uint8_t type = 0;
  uint32_t flags = 0;
  bool hasMetric = false;
};

// IPv4 address and main-table route operations on one network namespace. Every call is a
// single request/acknowledge exchange that the kernel completes synchronously.
class IpKernel {
 public:
  virtual ~IpKernel() = default;
  virtual int interfaceIndex(const std::string& name) = 0;
  virtual bool addresses(int ifindex, std::vector<KernelAddress>& out) = 0;
  virtual bool routes(std::vector<KernelRoute>& out) = 0;
  virtual bool addAddress(const KernelAddress& address) = 0;
  virtual bool deleteAddress(const KernelAddress& address) = 0;
  virtual bool replaceDefaultRoute(int ifindex, uint32_t gateway, bool onlink) = 0;
  virtual bool deleteRoute(const KernelRoute& route) = 0;
  virtual int lastError() const = 0;
};

// Request encoders and reply decoders, exposed for tests.
namespace netlink {
constexpr std::size_t kMaxRequest = 256;
struct Request {
  alignas(4) unsigned char bytes[kMaxRequest]{};
  std::size_t size = 0;
};
void addressRequest(Request& out, uint16_t type, uint16_t flags, uint32_t sequence,
                    const KernelAddress& address);
void defaultRouteRequest(Request& out, uint16_t type, uint16_t flags, uint32_t sequence, int ifindex,
                         uint32_t gateway, bool onlink);
void routeRequest(Request& out, uint16_t type, uint16_t flags, uint32_t sequence, const KernelRoute& route);
void dumpRequest(Request& out, uint16_t type, uint32_t sequence);
bool decodeAddress(const void* message, std::size_t size, KernelAddress& out);
bool decodeRoute(const void* message, std::size_t size, KernelRoute& out);
}

class Rtnetlink final : public IpKernel {
 public:
  Rtnetlink() = default;
  ~Rtnetlink() override;
  Rtnetlink(const Rtnetlink&) = delete;
  Rtnetlink& operator=(const Rtnetlink&) = delete;

  int interfaceIndex(const std::string& name) override;
  bool addresses(int ifindex, std::vector<KernelAddress>& out) override;
  bool routes(std::vector<KernelRoute>& out) override;
  bool addAddress(const KernelAddress& address) override;
  bool deleteAddress(const KernelAddress& address) override;
  bool replaceDefaultRoute(int ifindex, uint32_t gateway, bool onlink) override;
  bool deleteRoute(const KernelRoute& route) override;
  int lastError() const override { return error_; }

 private:
  using Visitor = void (*)(const void* message, std::size_t size, void* context);
  bool open();
  bool exchange(const netlink::Request& request, Visitor visitor, void* context);
  int fd_ = -1;
  uint32_t sequence_ = 0;
  int error_ = 0;
};

}
}
}
