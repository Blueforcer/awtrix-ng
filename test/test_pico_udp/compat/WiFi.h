#pragma once
#include "WiFiUdp.h"
struct TestWiFi {
  IPAddress localIP() { return IPAddress(42); }
  void macAddress(uint8_t* mac) { for (int i = 0; i < 6; ++i) mac[i] = i; }
};
inline TestWiFi WiFi;
