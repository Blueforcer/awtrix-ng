#include "platform/host/System.h"

#include <winsock2.h>
#include <ws2tcpip.h>
#include <iphlpapi.h>

#include <vector>

namespace awtrix::os {
namespace {

std::string firstAddress(const IP_ADAPTER_ADDRESSES* adapters, bool needGateway) {
  for (const auto* adapter = adapters; adapter; adapter = adapter->Next) {
    if (adapter->OperStatus != IfOperStatusUp || adapter->IfType == IF_TYPE_SOFTWARE_LOOPBACK) continue;
    if (needGateway && !adapter->FirstGatewayAddress) continue;
    for (const auto* unicast = adapter->FirstUnicastAddress; unicast; unicast = unicast->Next) {
      const sockaddr* address = unicast->Address.lpSockaddr;
      if (!address || address->sa_family != AF_INET) continue;
      char text[INET_ADDRSTRLEN]{};
      if (::inet_ntop(AF_INET, &reinterpret_cast<const sockaddr_in*>(address)->sin_addr, text, sizeof text))
        return text;
    }
  }
  return {};
}

}

std::string firstIpv4Address() {
  constexpr ULONG kFlags =
      GAA_FLAG_SKIP_ANYCAST | GAA_FLAG_SKIP_MULTICAST | GAA_FLAG_SKIP_DNS_SERVER | GAA_FLAG_INCLUDE_GATEWAYS;
  ULONG size = 16 * 1024;
  std::vector<unsigned char> buffer;
  ULONG status = ERROR_BUFFER_OVERFLOW;
  for (int attempt = 0; attempt < 3 && status == ERROR_BUFFER_OVERFLOW; ++attempt) {
    buffer.resize(size);
    status = ::GetAdaptersAddresses(AF_INET, kFlags, nullptr, reinterpret_cast<IP_ADAPTER_ADDRESSES*>(buffer.data()),
                                    &size);
  }
  if (status != NO_ERROR) return "127.0.0.1";
  const auto* adapters = reinterpret_cast<const IP_ADAPTER_ADDRESSES*>(buffer.data());
  std::string address = firstAddress(adapters, true);
  if (address.empty()) address = firstAddress(adapters, false);
  return address.empty() ? "127.0.0.1" : address;
}

std::string machineName() {
  SYSTEM_INFO info{};
  ::GetNativeSystemInfo(&info);
  switch (info.wProcessorArchitecture) {
    case PROCESSOR_ARCHITECTURE_AMD64: return "x86_64";
    case PROCESSOR_ARCHITECTURE_ARM64: return "aarch64";
    case PROCESSOR_ARCHITECTURE_INTEL: return "i686";
    default: return "";
  }
}

long secondsSinceBoot() { return static_cast<long>(::GetTickCount64() / 1000); }

}
