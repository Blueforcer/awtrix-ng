#pragma once

#include <cstdint>

// Template seam keeps arduino-pico's differing API contract host-testable.
// No board includes: callers supply WiFi, IPAddress and the mode constants.

namespace awtrix::platform::pico {
template <class Wifi, class Address>
void configureStatic(Wifi& wifi, Address ip, Address gateway, Address subnet,
                     Address dns1, Address dns2) {
  // Pico uses (ip, dns, gateway, subnet), unlike ESP32's five-argument overload.
  wifi.config(ip, dns1, gateway, subnet);
  wifi.setDNS(dns1, dns2);
}

// arduino-pico's begin() does not block once mode() has been called, and calling it again restarts
// the association. Only start a new join once the previous one has had its full timeout.
// 32-bit like the Pico's millis(), so the subtraction survives its 49-day wrap.
inline bool joinDue(uint32_t nowMs, uint32_t lastJoinMs, bool joinedBefore, uint32_t timeoutMs) {
  return !joinedBefore || nowMs - lastJoinMs >= timeoutMs;
}

// arduino-pico joins through the CYW43 firmware, which picks the AP for the SSID. The Pico does not
// pin a BSSID: on an eero mesh, pinned joins (the "join" iovar) to the node next to the board were
// refused with "network not found", while plain joins to that same node succeeded.
template <class Wifi, class Mode>
void join(Wifi& wifi, Mode mode, const char* ssid, const char* password) {
  // The pinned core's begin() changes _mode to STA even in AP_STA. Restore
  // AP_STA after each attempt so the NEXT retry cannot tear down the portal.
  wifi.mode(mode);
  wifi.begin(ssid, password);
  wifi.mode(mode);
  // ESP32 runs with esp_wifi_set_ps(WIFI_PS_NONE). The CYW43 default (CYW43_PERFORMANCE_PM) is
  // still power-save mode 2, which drops links on some mesh routers. begin() can re-initialise the
  // radio, so apply it after every join.
  wifi.noLowPowerMode();
}
}
