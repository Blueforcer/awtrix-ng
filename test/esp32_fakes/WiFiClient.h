#pragma once

#include <cstdint>

class WiFiClient {
 public:
  virtual ~WiFiClient() = default;
  void setTimeout(int) {}
  virtual uint8_t connected() { return 1; }
  virtual int available() { return 0; }
};
