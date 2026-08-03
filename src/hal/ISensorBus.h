#pragma once

namespace awtrix {

struct SensorReading {
  bool present = false;
  bool hasHumidity = false;
  bool hasPressure = false;
  float temperatureC = 0.0f;
  float humidity = 0.0f;
  float pressureHpa = 0.0f;
};

class ISensorBus {
 public:
  virtual ~ISensorBus() = default;
  virtual void begin() = 0;
  virtual bool hasSensor() const = 0;
  virtual SensorReading read() = 0;
  virtual const char* sensorName() const = 0;
  virtual bool hasHumidity() const { return hasSensor(); }
  virtual bool hasPressure() const { return false; }
};

}
