#pragma once

#include <cstdint>
#include <cstdio>
#include <string>

namespace awtrix {
namespace remote {

// The port a clock's process on the script host listens on when the address names none.
constexpr uint16_t kDefaultClockPort = 8301;

// "host" or "host:port"; false for an empty name or a port outside 1..65535.
inline bool splitHost(const std::string& spec, std::string& name, uint16_t& port) {
  const std::size_t colon = spec.find(':');
  name = spec.substr(0, colon);
  port = kDefaultClockPort;
  if (name.empty()) return false;
  if (colon == std::string::npos) return true;
  const std::size_t digits = spec.size() - colon - 1;
  if (digits == 0 || digits > 5) return false;
  long value = 0;
  for (std::size_t i = colon + 1; i < spec.size(); ++i) {
    const char c = spec[i];
    if (c < '0' || c > '9') return false;
    value = value * 10 + (c - '0');
  }
  if (value < 1 || value > 65535) return false;
  port = static_cast<uint16_t>(value);
  return true;
}

inline std::string addressText(uint32_t address) {
  char text[16];
  std::snprintf(text, sizeof(text), "%u.%u.%u.%u", static_cast<unsigned>(address >> 24),
                static_cast<unsigned>((address >> 16) & 0xFF), static_cast<unsigned>((address >> 8) & 0xFF),
                static_cast<unsigned>(address & 0xFF));
  return text;
}

inline std::string hostSpec(uint32_t address, uint16_t port) {
  return addressText(address) + ":" + std::to_string(port);
}

}
}
