#pragma once

#include <Arduino.h>

#include <functional>

namespace fake {
inline std::function<IPAddress(const char*)> mdnsLookup;
}

class MdnsFake {
 public:
  IPAddress queryHost(const char* label, uint32_t) {
    return fake::mdnsLookup ? fake::mdnsLookup(label) : IPAddress();
  }
};

inline MdnsFake MDNS;
