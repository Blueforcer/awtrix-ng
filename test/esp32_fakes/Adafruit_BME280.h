#pragma once

#include <cstdint>

class Adafruit_BME280 {
 public:
  static inline bool present = false;
  bool begin(uint8_t = 0x77) { return present; }
  float readTemperature() { return 21.0f; }
  float readHumidity() { return 40.0f; }
  float readPressure() { return 101325.0f; }
};
