#pragma once

#include <cstdint>

class Adafruit_SHT31 {
 public:
  static inline bool present = false;
  bool begin(uint8_t = 0x44) { return present; }
  bool readBoth(float* temperature, float* humidity) {
    *temperature = 21.0f;
    *humidity = 40.0f;
    return true;
  }
};
