#pragma once

#include <cstdint>

class Adafruit_BMP280 {
 public:
  static inline bool present = false;
  bool begin(uint8_t = 0x77) { return present; }
  float readTemperature() { return 21.0f; }
  float readPressure() { return 101325.0f; }
};
