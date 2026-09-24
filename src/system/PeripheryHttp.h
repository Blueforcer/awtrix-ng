#pragma once
#include <Arduino.h>
#include <HTTPClient.h>
#include <WiFiClient.h>
#include <string>

namespace awtrix {
// ESP32 transport adapter. The shared periphery itself has no network dependency.
inline void postButton(const std::string& url, const char* btn, bool state, const std::string& uid) {
  WiFiClient wc;
  HTTPClient http;
  http.setConnectTimeout(300);
  http.setTimeout(300);
  if (!http.begin(wc, url.c_str())) return;
  http.addHeader("Content-Type", "application/json");
  String body = String("{\"button\":\"") + btn + "\",\"state\":" + (state ? "true" : "false") +
                ",\"uid\":\"" + uid.c_str() + "\"}";
  http.POST(body);
  http.end();
}
}
