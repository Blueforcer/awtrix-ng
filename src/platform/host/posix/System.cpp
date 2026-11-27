#include "platform/host/System.h"

#include <arpa/inet.h>
#include <ifaddrs.h>
#include <net/if.h>
#include <sys/utsname.h>

#include <ctime>

#include "platform/posix/Time.h"

namespace awtrix::os {

std::string firstIpv4Address() {
  std::string address = "127.0.0.1";
  ifaddrs* interfaces = nullptr;
  if (getifaddrs(&interfaces) != 0) return address;
  for (auto* it = interfaces; it; it = it->ifa_next) {
    if (!it->ifa_addr || it->ifa_addr->sa_family != AF_INET || !(it->ifa_flags & IFF_UP) ||
        (it->ifa_flags & IFF_LOOPBACK))
      continue;
    char text[INET_ADDRSTRLEN]{};
    const auto* ipv4 = reinterpret_cast<sockaddr_in*>(it->ifa_addr);
    if (inet_ntop(AF_INET, &ipv4->sin_addr, text, sizeof(text))) address = text;
    break;
  }
  freeifaddrs(interfaces);
  return address;
}

std::string machineName() {
  utsname info{};
  return uname(&info) == 0 ? info.machine : "";
}

long secondsSinceBoot() {
#if defined(CLOCK_BOOTTIME)
  timespec now{};
  if (clock_gettime(CLOCK_BOOTTIME, &now) == 0) return static_cast<long>(now.tv_sec);
#endif
  return static_cast<long>(posix::monotonicMs() / 1000);
}

}
