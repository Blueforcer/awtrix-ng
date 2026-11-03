#pragma once

#include <Arduino.h>

#include <atomic>
#include <functional>

namespace fake {
inline std::atomic<bool> wifiUp{true};
inline std::function<int(const char*, IPAddress&)> dnsLookup;
}

class WiFiFake {
 public:
  bool isConnected() const { return fake::wifiUp; }
  int hostByName(const char* name, IPAddress& result) {
    return fake::dnsLookup ? fake::dnsLookup(name, result) : 0;
  }
};

inline WiFiFake WiFi;
