#pragma once
#include "hal/IBoard.h"

namespace awtrix {
// A headless Linux board: rendered frames are read over the API.
class LinuxBoard : public IBoard {
 public:
  LinuxBoard(int width, int height) : width_(width), height_(height) {}
  const char* name() const override { return "Linux"; }
  int matrixWidth() const override { return width_; }
  int matrixHeight() const override { return height_; }
  void begin() override {}
  void show(const Canvas&) override {}
  void setBrightness(uint8_t) override {}
  void setMatrixLayout(const MatrixLayout&) override {}
  void applyColorGrade(const render::GradeParams&) override {}
  bool hasBattery() const override { return battery_; }
  // A supervisor reports the battery; the board itself has no measurement path.
  void setBatteryPresent(bool present) { battery_ = present; }
  bool hasLightSensor() const override { return false; }
  int readBatteryMillivolts() override { return -1; }
  int readLdrRaw() override { return -1; }
  void pollButtons(ButtonState& out) override { out = {}; }
  sound::IToneSink* toneSink() override { return nullptr; }
  sound::ITrackSink* trackSink() override { return nullptr; }
  ISensorBus& sensors() override { return sensors_; }
 private:
  class NoSensors : public ISensorBus {
   public:
    void begin() override {}
    bool hasSensor() const override { return false; }
    SensorReading read() override { return {}; }
    const char* sensorName() const override { return "none"; }
  } sensors_;
  int width_, height_;
  bool battery_ = false;
};
} 
