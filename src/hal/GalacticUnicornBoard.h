#pragma once
#include "hal/IBoard.h"
#include "hal/GalacticUnicornDisplay.h"
#include "hal/GalacticUnicornDefaults.h"
#include <hardware/pio.h>

namespace awtrix {
DeviceConfig galacticUnicornDefaults();
// Single device-lifetime instance, called on core 0 only.
class GalacticUnicornBoard final : public IBoard {
 public:
  GalacticUnicornBoard() = default;
  explicit GalacticUnicornBoard(const DeviceConfig& cfg);
  GalacticUnicornBoard(const GalacticUnicornBoard&) = delete;
  GalacticUnicornBoard& operator=(const GalacticUnicornBoard&) = delete;
  const char* name() const override { return "Galactic Unicorn"; }
  int matrixWidth() const override { return galactic::Width; }
  int matrixHeight() const override { return height_; }
  void begin() override;
  void show(const Canvas&) override;
  void logRefreshProgress() const;
  void setBrightness(uint8_t brightness) override { brightness_ = brightness; updateGrade(); }
  // Only the drawing transforms apply; width, chaining and wiring are fixed by the hardware.
  void setMatrixLayout(const MatrixLayout& layout) override {
    orientation_ = {layout.mirror, layout.rotate180};
  }
  void applyColorGrade(const render::GradeParams& grade) override { baseGrade_ = grade; updateGrade(); }
  bool hasBattery() const override { return false; }
  bool hasLightSensor() const override { return true; }
  int readBatteryMillivolts() override { return -1; }
  int readLdrRaw() override;
  void pollButtons(ButtonState& out) override;
  std::array<bool, 9> readInputs() const;
  sound::IToneSink* toneSink() override { return nullptr; }
  sound::ITrackSink* trackSink() override { return nullptr; }
  ISensorBus& sensors() override { return sensors_; }
 private:
  void updateGrade() { auto p = baseGrade_; p.brightness = brightness_; grade_.setParams(p); }
  class NoSensors final : public ISensorBus {
   public:
    void begin() override {}
    bool hasSensor() const override { return false; }
    SensorReading read() override { return {}; }
    const char* sensorName() const override { return "none"; }
  } sensors_;
  int height_ = 11;
  bool invalidHeight_ = false, ready_ = false;
  uint8_t brightness_ = 120;
  render::GradeParams baseGrade_;
  galactic::Grade14 grade_;
  galactic::Orientation orientation_;
  galactic::Stream buffers_[2];
  // Aligned atomic pointer publication; only DMA reads this after publication.
  alignas(4) volatile uintptr_t nextStream_ = 0;
  int front_ = 0;
  PIO pio_ = nullptr;
  int sm_ = -1, dataDma_ = -1, controlDma_ = -1;
};
}
