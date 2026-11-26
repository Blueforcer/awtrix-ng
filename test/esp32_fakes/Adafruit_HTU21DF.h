#pragma once

class Adafruit_HTU21DF {
 public:
  static inline bool present = false;
  bool begin() { return present; }
  float readTemperature() { return 21.0f; }
  float readHumidity() { return 40.0f; }
};
