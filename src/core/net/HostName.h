#pragma once

#include <cstddef>
#include <cstdint>
#include <string>

namespace awtrix {
namespace net {

// Loops one position past the end and treats that as a trailing '.', so the last octet needs no
// special case after the loop.
inline bool parseIpv4(const std::string& s, uint8_t out[4]) {
  int part = 0;
  int value = -1;
  for (std::size_t i = 0; i <= s.size(); ++i) {
    const char c = i < s.size() ? s[i] : '.';
    if (c >= '0' && c <= '9') {
      value = (value < 0 ? 0 : value) * 10 + (c - '0');
      if (value > 255) return false;
    } else if (c == '.') {
      if (value < 0 || part > 3) return false;
      out[part++] = static_cast<uint8_t>(value);
      value = -1;
    } else {
      return false;
    }
  }
  return part == 4;
}

inline bool isMdnsName(const std::string& host) {
  static const char kSuffix[] = ".local";
  const std::size_t n = sizeof(kSuffix) - 1;
  if (host.size() <= n) return false;
  for (std::size_t i = 0; i < n; ++i) {
    char c = host[host.size() - n + i];
    if (c >= 'A' && c <= 'Z') c = static_cast<char>(c - 'A' + 'a');
    if (c != kSuffix[i]) return false;
  }
  return true;
}

inline std::string mdnsLabel(const std::string& host) {
  if (!isMdnsName(host)) return host;
  return host.substr(0, host.size() - (sizeof(".local") - 1));
}

// "awtrixng-" plus the last three MAC bytes as lowercase hex, accepting the MAC with or without
// separators. This is the name the device announces when the user has not set one.
inline std::string defaultHostname(const std::string& mac) {
  std::string m;
  for (const char c : mac) {
    if (c == ':' || c == '-') continue;
    m += (c >= 'A' && c <= 'Z') ? static_cast<char>(c - 'A' + 'a') : c;
  }
  return "awtrixng-" + (m.size() >= 6 ? m.substr(m.size() - 6) : m);
}

inline std::string effectiveHostname(const std::string& configured, const std::string& mac) {
  return configured.empty() ? defaultHostname(mac) : configured;
}

}
}
