#include "platform/tc002/daemon/ip/Netlink.h"

#include <arpa/inet.h>
#include <cerrno>
#include <cstddef>
#include <cstring>
#include <ctime>
#include <linux/if_addr.h>
#include <linux/netlink.h>
#include <linux/rtnetlink.h>
#include <net/if.h>
#include <poll.h>
#include <sys/socket.h>
#include <unistd.h>

#include "platform/posix/Files.h"

namespace awtrix {
namespace tc002d {
namespace ip {
namespace netlink {
namespace {

template <typename Header>
Header* begin(Request& out, uint16_t type, uint16_t flags, uint32_t sequence) {
  out = Request{};
  nlmsghdr header{};
  header.nlmsg_len = NLMSG_LENGTH(sizeof(Header));
  header.nlmsg_type = type;
  header.nlmsg_flags = flags;
  header.nlmsg_seq = sequence;
  std::memcpy(out.bytes, &header, sizeof(header));
  out.size = header.nlmsg_len;
  return reinterpret_cast<Header*>(out.bytes + NLMSG_HDRLEN);
}

void attribute(Request& out, uint16_t type, const void* data, std::size_t size) {
  const std::size_t at = NLMSG_ALIGN(out.size);
  const std::size_t length = RTA_LENGTH(size);
  if (at + RTA_ALIGN(length) > kMaxRequest) return;
  rtattr header{};
  header.rta_type = type;
  header.rta_len = static_cast<unsigned short>(length);
  std::memcpy(out.bytes + at, &header, sizeof(header));
  std::memcpy(out.bytes + at + RTA_LENGTH(0), data, size);
  out.size = at + RTA_ALIGN(length);
  const uint32_t total = static_cast<uint32_t>(out.size);
  std::memcpy(out.bytes + offsetof(nlmsghdr, nlmsg_len), &total, sizeof(total));
}

void addressAttribute(Request& out, uint16_t type, uint32_t hostOrder) {
  const uint32_t network = htonl(hostOrder);
  attribute(out, type, &network, sizeof(network));
}

void numberAttribute(Request& out, uint16_t type, uint32_t value) {
  attribute(out, type, &value, sizeof(value));
}

template <typename Body, typename Visit>
bool attributes(const void* message, std::size_t size, Body& body, Visit visit) {
  const auto* bytes = static_cast<const unsigned char*>(message);
  const std::size_t start = NLMSG_ALIGN(NLMSG_LENGTH(sizeof(Body)));
  if (size < NLMSG_LENGTH(sizeof(Body))) return false;
  std::memcpy(&body, bytes + NLMSG_HDRLEN, sizeof(Body));
  std::size_t at = start;
  while (at + sizeof(rtattr) <= size) {
    rtattr header{};
    std::memcpy(&header, bytes + at, sizeof(header));
    if (header.rta_len < sizeof(rtattr) || at + header.rta_len > size) return false;
    visit(header.rta_type, bytes + at + RTA_LENGTH(0), header.rta_len - RTA_LENGTH(0));
    at += RTA_ALIGN(header.rta_len);
  }
  return true;
}

bool readAddress(const unsigned char* data, std::size_t size, uint32_t& out) {
  if (size != 4) return false;
  uint32_t network = 0;
  std::memcpy(&network, data, 4);
  out = ntohl(network);
  return true;
}

bool readNumber(const unsigned char* data, std::size_t size, uint32_t& out) {
  if (size != 4) return false;
  std::memcpy(&out, data, 4);
  return true;
}

}

void addressRequest(Request& out, uint16_t type, uint16_t flags, uint32_t sequence,
                    const KernelAddress& address) {
  auto* body = begin<ifaddrmsg>(out, type, flags, sequence);
  body->ifa_family = AF_INET;
  body->ifa_prefixlen = address.prefix;
  body->ifa_scope = RT_SCOPE_UNIVERSE;
  body->ifa_index = static_cast<uint32_t>(address.ifindex);
  addressAttribute(out, IFA_LOCAL, address.address);
  addressAttribute(out, IFA_ADDRESS, address.address);
  if (address.broadcast && type == RTM_NEWADDR) addressAttribute(out, IFA_BROADCAST, address.broadcast);
}

void defaultRouteRequest(Request& out, uint16_t type, uint16_t flags, uint32_t sequence, int ifindex,
                         uint32_t gateway, bool onlink) {
  auto* body = begin<rtmsg>(out, type, flags, sequence);
  body->rtm_family = AF_INET;
  body->rtm_table = RT_TABLE_MAIN;
  body->rtm_protocol = RTPROT_DHCP;
  body->rtm_scope = RT_SCOPE_UNIVERSE;
  body->rtm_type = RTN_UNICAST;
  body->rtm_flags = onlink ? RTNH_F_ONLINK : 0;
  addressAttribute(out, RTA_GATEWAY, gateway);
  numberAttribute(out, RTA_OIF, static_cast<uint32_t>(ifindex));
}

void routeRequest(Request& out, uint16_t type, uint16_t flags, uint32_t sequence, const KernelRoute& route) {
  auto* body = begin<rtmsg>(out, type, flags, sequence);
  body->rtm_family = AF_INET;
  body->rtm_dst_len = route.prefix;
  body->rtm_table = static_cast<unsigned char>(route.table < 256 ? route.table : unsigned(RT_TABLE_UNSPEC));
  body->rtm_scope = RT_SCOPE_NOWHERE;
  if (route.table >= 256) numberAttribute(out, RTA_TABLE, route.table);
  if (route.prefix) addressAttribute(out, RTA_DST, route.destination);
  if (route.gateway) addressAttribute(out, RTA_GATEWAY, route.gateway);
  if (route.ifindex) numberAttribute(out, RTA_OIF, static_cast<uint32_t>(route.ifindex));
  if (route.hasMetric) numberAttribute(out, RTA_PRIORITY, route.metric);
}

void dumpRequest(Request& out, uint16_t type, uint32_t sequence) {
  const uint16_t flags = NLM_F_REQUEST | NLM_F_DUMP;
  if (type == RTM_GETADDR) begin<ifaddrmsg>(out, type, flags, sequence)->ifa_family = AF_INET;
  else begin<rtmsg>(out, type, flags, sequence)->rtm_family = AF_INET;
}

bool decodeAddress(const void* message, std::size_t size, KernelAddress& out) {
  out = KernelAddress{};
  ifaddrmsg body{};
  uint32_t local = 0, peer = 0, flags = 0;
  bool hasLocal = false, hasPeer = false, hasFlags = false;
  const bool okay = attributes(message, size, body, [&](uint16_t type, const unsigned char* data, std::size_t length) {
    if (type == IFA_LOCAL) hasLocal = readAddress(data, length, local);
    else if (type == IFA_ADDRESS) hasPeer = readAddress(data, length, peer);
    else if (type == IFA_BROADCAST) readAddress(data, length, out.broadcast);
    else if (type == IFA_FLAGS) hasFlags = readNumber(data, length, flags);
  });
  if (!okay || body.ifa_family != AF_INET || (!hasLocal && !hasPeer)) return false;
  out.ifindex = static_cast<int>(body.ifa_index);
  out.prefix = body.ifa_prefixlen;
  out.address = hasLocal ? local : peer;
  out.flags = hasFlags ? static_cast<uint8_t>(flags) : body.ifa_flags;
  return true;
}

bool decodeRoute(const void* message, std::size_t size, KernelRoute& out) {
  out = KernelRoute{};
  rtmsg body{};
  uint32_t table = 0, oif = 0;
  bool hasTable = false;
  const bool okay = attributes(message, size, body, [&](uint16_t type, const unsigned char* data, std::size_t length) {
    if (type == RTA_DST) readAddress(data, length, out.destination);
    else if (type == RTA_GATEWAY) readAddress(data, length, out.gateway);
    else if (type == RTA_OIF && readNumber(data, length, oif)) out.ifindex = static_cast<int>(oif);
    else if (type == RTA_PRIORITY) out.hasMetric = readNumber(data, length, out.metric);
    else if (type == RTA_TABLE) hasTable = readNumber(data, length, table);
  });
  if (!okay || body.rtm_family != AF_INET) return false;
  out.prefix = body.rtm_dst_len;
  out.table = hasTable ? table : body.rtm_table;
  out.protocol = body.rtm_protocol;
  out.scope = body.rtm_scope;
  out.type = body.rtm_type;
  out.flags = body.rtm_flags;
  return true;
}

}

namespace {

constexpr int kExchangeBudgetMs = 40;

void collectAddress(const void* message, std::size_t size, void* context) {
  auto& out = *static_cast<std::pair<int, std::vector<KernelAddress>*>*>(context);
  KernelAddress address;
  if (netlink::decodeAddress(message, size, address) && (!out.first || address.ifindex == out.first))
    out.second->push_back(address);
}

void collectRoute(const void* message, std::size_t size, void* context) {
  KernelRoute route;
  if (netlink::decodeRoute(message, size, route) && route.table == RT_TABLE_MAIN)
    static_cast<std::vector<KernelRoute>*>(context)->push_back(route);
}

}

Rtnetlink::~Rtnetlink() {
  if (fd_ >= 0) ::close(fd_);
}

bool Rtnetlink::open() {
  if (fd_ >= 0) return true;
  fd_ = ::socket(AF_NETLINK, SOCK_RAW | SOCK_CLOEXEC | SOCK_NONBLOCK, NETLINK_ROUTE);
  if (fd_ < 0) {
    error_ = errno;
    return false;
  }
  sockaddr_nl local{};
  local.nl_family = AF_NETLINK;
  if (::bind(fd_, reinterpret_cast<sockaddr*>(&local), sizeof(local)) != 0) {
    error_ = errno;
    ::close(fd_);
    fd_ = -1;
    return false;
  }
  return true;
}

bool Rtnetlink::exchange(const netlink::Request& request, Visitor visitor, void* context) {
  error_ = 0;
  if (!open()) return false;
  nlmsghdr sent{};
  std::memcpy(&sent, request.bytes, sizeof(sent));
  sockaddr_nl kernel{};
  kernel.nl_family = AF_NETLINK;
  if (::sendto(fd_, request.bytes, request.size, 0, reinterpret_cast<sockaddr*>(&kernel), sizeof(kernel)) !=
      static_cast<ssize_t>(request.size)) {
    error_ = errno ? errno : EIO;
    return false;
  }
  const int64_t deadline = posix::monotonicMs() + kExchangeBudgetMs;
  alignas(4) static unsigned char buffer[16384];
  for (;;) {
    const int64_t remaining = deadline - posix::monotonicMs();
    if (remaining <= 0) {
      error_ = ETIMEDOUT;
      return false;
    }
    pollfd waiter{fd_, POLLIN, 0};
    const int ready = ::poll(&waiter, 1, static_cast<int>(remaining));
    if (ready < 0 && errno == EINTR) continue;
    if (ready <= 0) {
      error_ = ready == 0 ? ETIMEDOUT : errno;
      return false;
    }
    sockaddr_nl sender{};
    socklen_t senderSize = sizeof(sender);
    const ssize_t received = ::recvfrom(fd_, buffer, sizeof(buffer), 0, reinterpret_cast<sockaddr*>(&sender), &senderSize);
    if (received < 0) {
      if (errno == EINTR || errno == EAGAIN) continue;
      error_ = errno;
      return false;
    }
    if (sender.nl_pid != 0) continue;
    std::size_t at = 0;
    const std::size_t total = static_cast<std::size_t>(received);
    while (at + sizeof(nlmsghdr) <= total) {
      nlmsghdr header{};
      std::memcpy(&header, buffer + at, sizeof(header));
      if (header.nlmsg_len < sizeof(nlmsghdr) || at + header.nlmsg_len > total) {
        error_ = EPROTO;
        return false;
      }
      const std::size_t next = at + NLMSG_ALIGN(header.nlmsg_len);
      if (header.nlmsg_seq != sent.nlmsg_seq) {
        at = next;
        continue;
      }
      if (header.nlmsg_type == NLMSG_ERROR) {
        nlmsgerr failure{};
        if (header.nlmsg_len < NLMSG_LENGTH(sizeof(nlmsgerr))) {
          error_ = EPROTO;
          return false;
        }
        std::memcpy(&failure, buffer + at + NLMSG_HDRLEN, sizeof(failure));
        error_ = -failure.error;
        return failure.error == 0;
      }
      if (header.nlmsg_type == NLMSG_DONE) {
        if (header.nlmsg_flags & NLM_F_DUMP_INTR) {
          error_ = EAGAIN;
          return false;
        }
        return true;
      }
      if (header.nlmsg_type != NLMSG_NOOP && visitor) visitor(buffer + at, header.nlmsg_len, context);
      at = next;
    }
  }
}

int Rtnetlink::interfaceIndex(const std::string& name) {
  const unsigned index = if_nametoindex(name.c_str());
  error_ = index ? 0 : errno;
  return static_cast<int>(index);
}

bool Rtnetlink::addresses(int ifindex, std::vector<KernelAddress>& out) {
  out.clear();
  netlink::Request request;
  netlink::dumpRequest(request, RTM_GETADDR, ++sequence_);
  std::pair<int, std::vector<KernelAddress>*> context{ifindex, &out};
  return exchange(request, collectAddress, &context);
}

bool Rtnetlink::routes(std::vector<KernelRoute>& out) {
  out.clear();
  netlink::Request request;
  netlink::dumpRequest(request, RTM_GETROUTE, ++sequence_);
  return exchange(request, collectRoute, &out);
}

bool Rtnetlink::addAddress(const KernelAddress& address) {
  netlink::Request request;
  netlink::addressRequest(request, RTM_NEWADDR, NLM_F_REQUEST | NLM_F_ACK | NLM_F_CREATE | NLM_F_REPLACE,
                          ++sequence_, address);
  return exchange(request, nullptr, nullptr);
}

bool Rtnetlink::deleteAddress(const KernelAddress& address) {
  netlink::Request request;
  netlink::addressRequest(request, RTM_DELADDR, NLM_F_REQUEST | NLM_F_ACK, ++sequence_, address);
  return exchange(request, nullptr, nullptr);
}

bool Rtnetlink::replaceDefaultRoute(int ifindex, uint32_t gateway, bool onlink) {
  netlink::Request request;
  netlink::defaultRouteRequest(request, RTM_NEWROUTE, NLM_F_REQUEST | NLM_F_ACK | NLM_F_CREATE | NLM_F_REPLACE,
                               ++sequence_, ifindex, gateway, onlink);
  return exchange(request, nullptr, nullptr);
}

bool Rtnetlink::deleteRoute(const KernelRoute& route) {
  netlink::Request request;
  netlink::routeRequest(request, RTM_DELROUTE, NLM_F_REQUEST | NLM_F_ACK, ++sequence_, route);
  return exchange(request, nullptr, nullptr);
}

}
}
}
