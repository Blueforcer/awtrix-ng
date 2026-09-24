#pragma once

#include "hal/IBoard.h"
#include "persistence/DeviceConfig.h"

namespace awtrix {

// Board-local defaults, without changing the defaults of existing installations.
DeviceConfig galacticUnicornDefaults();

class GalacticUnicornBoard final : public IBoard {
 public:
  GalacticUnicornBoard() = default;
  explicit GalacticUnicornBoard(const DeviceConfig& cfg);
  const char* name() const override { return "Galactic Unicorn"; }
  int matrixWidth() const override { return 53; }
  int matrixHeight() const override { return height_; }
  void begin() override {}
  // No panel driver yet, so no GPIO is touched.
  void show(const Canvas&) override {}
  void setBrightness(uint8_t) override {}
  void setMatrixLayout(const MatrixLayout&) override {}
  void applyColorGrade(const render::GradeParams&) override {}
  bool hasBattery() const override { return false; }
  bool hasLightSensor() const override { return false; }
  int readBatteryMillivolts() override { return -1; }
  int readLdrRaw() override { return -1; }
  void pollButtons(ButtonState& out) override { out = {}; }
  sound::IToneSink* toneSink() override { return nullptr; }
  sound::ITrackSink* trackSink() override { return nullptr; }
  ISensorBus& sensors() override { return sensors_; }

 private:
  class NoSensors final : public ISensorBus {
   public:
    void begin() override {}
    bool hasSensor() const override { return false; }
    SensorReading read() override { return {}; }
    const char* sensorName() const override { return "none"; }
  } sensors_;
  int height_ = 11;
};

}
