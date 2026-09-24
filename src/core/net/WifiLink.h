#pragma once

#include <cstdint>
#include <string>

#include "core/net/LinkStatus.h"

namespace awtrix {
namespace net {

// The radio's association state, reduced to what the status mapping cares about. NetworkService
// folds the ESP32 wl_status_t values onto these; tests and the simulator supply them directly.
enum class WifiAssoc : uint8_t {
  Idle,
  Joining,
  Connected,
  NoSsidFound,
  AuthFailed,
  Disconnected,
};

// Folds one observation of the radio into the link status. Pure, so the mapping can be tested on
// the host: NetworkService itself drags in <WiFi.h> and <esp_wifi.h> and cannot be.
//
// Idempotent - calling it twice with the same observation changes nothing - so it is safe to run
// at any rate. The failed-attempt counter lives in noteWifiRetry() for exactly that reason.
inline void applyWifiAssoc(LinkStatus& s, WifiAssoc assoc, bool hasSsid, const std::string& ssid,
                           const std::string& ip) {
  s.enabled = hasSsid;
  s.host = ssid;
  if (!hasSsid) {
    s.phase = LinkPhase::Disabled;
    s.setError(LinkError::None);
    s.endpoint.clear();
    s.retryInMs = 0;
    return;
  }

  const bool wasConnected = s.phase == LinkPhase::Connected;

  if (assoc == WifiAssoc::Connected) {
    if (!wasConnected) ++s.connects;
    s.phase = LinkPhase::Connected;
    s.setError(LinkError::None);
    s.endpoint = ip;
    s.attempts = 0;
    s.retryInMs = 0;
    return;
  }

  s.endpoint.clear();
  s.phase = assoc == WifiAssoc::Joining ? LinkPhase::Connecting : LinkPhase::Offline;
  switch (assoc) {
    case WifiAssoc::NoSsidFound:
      // The SSID is this link's host, so "the configured network is not on the air" is the same
      // shape of failure as an unresolvable broker name.
      s.setError(LinkError::HostNotFound);
      break;
    case WifiAssoc::AuthFailed:
      s.setError(LinkError::BadCredentials);
      break;
    case WifiAssoc::Joining:
      s.error = LinkError::None;
      s.retryInMs = 0;
      break;
    default:
      // A link that was up a moment ago reports the drop. Otherwise whatever reason was recorded
      // when the join failed stays put; the radio reports plain "disconnected" from then on.
      if (wasConnected) {
        s.setError(LinkError::Lost);
        s.attempts = 0;
        s.retryInMs = 0;
      }
      break;
  }
}

// CYW43 reports an AP that breaks off a join mid key exchange - common right after an unclean
// reset, while the AP still holds the old association - with the same BADAUTH as a wrong password,
// and the next join then succeeds. The ESP32 core reports WL_CONNECT_FAILED only for a real
// authentication failure. This gate gives radios like CYW43 the same outcome: an authentication
// failure means bad credentials once two join attempts in a row have ended in one; a single one
// reads as a plain disconnect.
class AuthFailureGate {
 public:
  // Call as each join attempt is issued, with what the radio reported for the attempt before it.
  void noteJoin(WifiAssoc previous) {
    if (previous != WifiAssoc::AuthFailed) failedJoins_ = 0;
    else if (failedJoins_ < UINT8_MAX) ++failedJoins_;
  }

  // Passes one observation of the radio through, holding back an authentication failure until the
  // join before this one failed the same way.
  WifiAssoc filter(WifiAssoc assoc) {
    if (assoc == WifiAssoc::Connected) failedJoins_ = 0;
    if (assoc == WifiAssoc::AuthFailed && failedJoins_ == 0) return WifiAssoc::Disconnected;
    return assoc;
  }

 private:
  uint8_t failedJoins_ = 0;
};

// Records that a reconnect was just issued and when the next one is due. Split out of
// applyWifiAssoc so the counter tracks retries rather than how often the caller polls.
inline void noteWifiRetry(LinkStatus& s, uint32_t retryInMs) {
  if (s.attempts < UINT16_MAX) ++s.attempts;
  s.retryInMs = retryInMs;
}

}
}
