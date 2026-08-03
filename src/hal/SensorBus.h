#pragma once

#include "hal/ISensorBus.h"

namespace awtrix {

// Pimpl so the Adafruit and Wire headers stay out of everything that pulls in a board.
class SensorBus : public ISensorBus {
 public:
  SensorBus();
  ~SensorBus() override;
  void setPins(int sda, int scl);
  void begin() override;
  bool hasSensor() const override;
  bool hasHumidity() const override;
  bool hasPressure() const override;
  SensorReading read() override;
  const char* sensorName() const override;

 private:
  struct Impl;
  Impl* impl_;
};

}
