#include "platform/linux/net/PrivateHost.h"

#include <arpa/inet.h>

#include <algorithm>
#include <array>
#include <cctype>
#include <cstring>
#include <string>

#include "core/StrCase.h"

namespace awtrix::net {
namespace {

bool parseIpv4(std::string_view host, std::array<int, 4>& out) {
  std::size_t at = 0;
  for (std::size_t block = 0; block < out.size(); ++block) {
    if (block > 0 && (at >= host.size() || host[at++] != '.')) return false;
    const std::size_t start = at;
    int value = 0;
    while (at < host.size() && at - start < 3 && host[at] >= '0' && host[at] <= '9')
      value = value * 10 + (host[at++] - '0');
    const std::size_t digits = at - start;
    if (digits == 0 || value > 255 || (digits > 1 && host[start] == '0')) return false;
    out[block] = value;
  }
  return at == host.size();
}

bool privateIpv4(const std::array<int, 4>& a) {
  return a[0] == 10 || a[0] == 127 || (a[0] == 172 && a[1] >= 16 && a[1] <= 31) || (a[0] == 192 && a[1] == 168) ||
         (a[0] == 169 && a[1] == 254);
}

bool privateIpv6(std::string_view host) {
  if (host.size() < 2 || host.front() != '[' || host.back() != ']') return false;
  const std::string address(host.substr(1, host.size() - 2));
  unsigned char bytes[16];
  if (::inet_pton(AF_INET6, address.c_str(), bytes) != 1) return false;
  static constexpr unsigned char kLoopback[16] = {0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 1};
  return std::memcmp(bytes, kLoopback, sizeof bytes) == 0 || (bytes[0] & 0xfe) == 0xfc ||
         (bytes[0] == 0xfe && (bytes[1] & 0xc0) == 0x80);
}

bool localName(std::string_view host) {
  constexpr std::string_view kSuffix = ".local";
  return host.size() > kSuffix.size() &&
         strcase::equalsIgnoreCase(host.substr(host.size() - kSuffix.size()), kSuffix) &&
         std::all_of(host.begin(), host.end(), [](unsigned char c) {
           return std::isalnum(c) || c == '.' || c == '-' || c == '_';
         });
}

}

bool isPrivateHost(std::string_view host) {
  std::array<int, 4> ipv4{};
  return parseIpv4(host, ipv4) ? privateIpv4(ipv4) : privateIpv6(host) || localName(host);
}

}
