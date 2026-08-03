#pragma once


#include <cstdint>

#include "IPAddress.h"

// Pretends the host is permanently associated: fixed loopback address and RSSI so the status pages
// and MQTT announcements have something plausible to report.
class WiFiClass {
 public:
  bool isConnected() const { return true; }
  IPAddress localIP() const { return IPAddress(127, 0, 0, 1); }
  int32_t RSSI() const { return -55; }
  const char* getHostname() const { return hostname_; }
  void setHostname(const char* h) { hostname_ = h ? h : ""; }

 private:
  const char* hostname_ = "";
};

inline WiFiClass WiFi;
