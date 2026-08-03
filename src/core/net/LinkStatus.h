#pragma once

#include <cstdint>
#include <string>

namespace awtrix {
namespace net {

enum class LinkPhase : uint8_t { Disabled, Offline, Connecting, Connected };

enum class LinkError : uint8_t {
  None,
  NoWifi,
  HostNotFound,
  Refused,
  BadCredentials,
  Rejected,
  Timeout,
  Lost
};

// These two names go straight into the API state JSON, so they are part of the public contract.
// LinkError::None deliberately maps to an empty string, which callers use to omit the field.
inline const char* linkPhaseName(LinkPhase p) {
  switch (p) {
    case LinkPhase::Disabled:   return "disabled";
    case LinkPhase::Offline:    return "offline";
    case LinkPhase::Connecting: return "connecting";
    case LinkPhase::Connected:  return "connected";
  }
  return "offline";
}

inline const char* linkErrorName(LinkError e) {
  switch (e) {
    case LinkError::None:           return "";
    case LinkError::NoWifi:         return "noWifi";
    case LinkError::HostNotFound:   return "hostNotFound";
    case LinkError::Refused:        return "refused";
    case LinkError::BadCredentials: return "badCredentials";
    case LinkError::Rejected:       return "rejected";
    case LinkError::Timeout:        return "timeout";
    case LinkError::Lost:           return "lost";
  }
  return "";
}

struct LinkStatus {
  LinkPhase phase = LinkPhase::Disabled;
  LinkError error = LinkError::None;
  // The last reason this link went down, kept after it recovers. For Wi-Fi that is the only way
  // the reason is ever readable: while the link is down, nothing can reach the API to ask.
  LinkError lastError = LinkError::None;
  bool enabled = false;
  std::string host;
  std::string endpoint;
  uint16_t attempts = 0;
  uint32_t retryInMs = 0;
  uint32_t connects = 0;

  void setError(LinkError e) {
    error = e;
    if (e != LinkError::None) lastError = e;
  }
};

}
}
