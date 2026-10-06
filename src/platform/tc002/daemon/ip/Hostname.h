#include "platform/posix/Bytes.h"
#pragma once

#include <cstdint>
#include <string>
#include <string_view>

#include "core/net/HostName.h"
#include "platform/posix/Files.h"

namespace awtrix {
namespace tc002d {
namespace ip {

// One RFC 1123 label: 1..63 letters, digits and inner hyphens.
inline bool validHostname(std::string_view name) {
  if (name.empty() || name.size() > 63 || name.front() == '-' || name.back() == '-') return false;
  for (char c : name) {
    const bool letter = (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z');
    if (!letter && !(c >= '0' && c <= '9') && c != '-') return false;
  }
  return true;
}

inline bool parseMac(std::string_view text, uint8_t (&out)[6]) {
  while (!text.empty() && (text.back() == '\n' || text.back() == '\r' || text.back() == ' ')) text.remove_suffix(1);
  return posix::parseMac(text, out);
}


// The ESP32 default name (net::defaultHostname) for a MAC as sysfs or the Wi-Fi service report
// it; empty for an invalid MAC.
inline std::string hostnameForMac(std::string_view mac) {
  uint8_t bytes[6];
  if (!parseMac(mac, bytes)) return {};
  return net::defaultHostname(posix::hexBytes(bytes, 6));
}

// Twelve lowercase hex digits, the mDNS TXT "id" value as on ESP32; empty for an invalid MAC.
inline std::string macId(std::string_view mac) {
  uint8_t bytes[6];
  return parseMac(mac, bytes) ? posix::hexBytes(bytes, 6) : std::string();
}

}
}
}
