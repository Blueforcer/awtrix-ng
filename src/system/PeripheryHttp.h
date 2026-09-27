#pragma once
#include <Arduino.h>
#include <HTTPClient.h>
#include <WiFiClient.h>
#include <string>

#include "core/api/ButtonEventJson.h"

namespace awtrix {
// ESP32 and Pico transport adapter for the buttonCallback webhook. The shared periphery itself has
// no network dependency. It runs in the loop, so a dead listener may cost at most 300 ms per wait.
inline void postButton(const std::string& url, const char* btn, bool state, const std::string& uid) {
  WiFiClient wc;
  HTTPClient http;
#if defined(AWTRIX_PLATFORM_RP2040)
  // arduino-pico's HTTPClient has no connect timeout; the client's own timeout bounds the connect.
  wc.setTimeout(300);
#else
  http.setConnectTimeout(300);
#endif
  http.setTimeout(300);
  if (!http.begin(wc, url.c_str())) return;
  http.addHeader("Content-Type", "application/json");
  http.POST(String(api::buttonEventJson(btn, state, uid).c_str()));
  http.end();
}
}
