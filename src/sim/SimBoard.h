#pragma once

#include <cstdint>

#include <cstdio>
#include <functional>
#include <memory>

#include "core/render/MatrixLayout.h"
#include "hal/IBoard.h"
#include "system/Log.h"

namespace awtrix {

// No audio on the host: every request is logged and reported as finished immediately, so apps that
// wait for a melody to end never block.
class SimSound : public ISoundBackend {
 public:
  void begin() override {}
  void setVolume(uint8_t volume) override { volume_ = volume; }
  bool playFile(const std::string& id) override {
    logf("sim sound: play file '%s'", id.c_str());
    return true;
  }
  void playRtttl(const std::string& rtttl) override {
    logf("sim sound: rtttl '%.60s%s'", rtttl.c_str(), rtttl.size() > 60 ? "..." : "");
  }
  void stop() override {}
  void tick() override {}
  bool isPlaying() const override { return false; }

 private:
  uint8_t volume_ = 25;
};

// Always "present" with whatever values the /sim/sensors route last wrote into the public fields.
class SimSensors : public ISensorBus {
 public:
  void begin() override {}
  bool hasSensor() const override { return true; }
  SensorReading read() override {
    SensorReading r;
    r.present = true;
    r.hasHumidity = true;
    r.temperatureC = temperatureC;
    r.humidity = humidity;
    return r;
  }
  const char* sensorName() const override { return "SIM"; }

  float temperatureC = 21.5f;
  float humidity = 42.0f;
};

class SimBoard : public IBoard {
 public:
  const char* name() const override { return "Simulator"; }
  int matrixWidth() const override { return layout_.width(); }
  int matrixHeight() const override { return layout_.height(); }

  void begin() override {}
  // On the device the colour grade is applied inside the LED driver, so grade here too before the
  // frame is handed on; graded_ is a scratch canvas kept only while a non-identity grade is set.
  void show(const Canvas& canvas) override {
    ++framesShown_;
    if (!onShow) return;
    if (grade_.isIdentity()) {
      onShow(canvas, brightness_);
      return;
    }
    if (!graded_ || graded_->width() != canvas.width() || graded_->height() != canvas.height())
      graded_.reset(new Canvas(canvas.width(), canvas.height()));
    grade_.apply(canvas, *graded_);
    onShow(*graded_, brightness_);
  }
  void setBrightness(uint8_t brightness) override { brightness_ = brightness; }
  void setMatrixLayout(const MatrixLayout& layout) override { layout_ = layout; }
  void applyColorGrade(const render::GradeParams& grade) override { grade_.setParams(grade); }

  bool hasBattery() const override { return true; }
  bool hasLightSensor() const override { return true; }
  int readBatteryMillivolts() override { return batteryPinMillivolts; }
  int readLdrRaw() override { return ldrRaw; }
  // Presses are "held until a deadline" that the /sim/button route sets, which is why they have to
  // outlast the 35 ms debounce to register at all.
  void pollButtons(ButtonState& out) override {
    const int64_t now = nowMs_;
    out.left = now < leftUntilMs;
    out.select = now < selectUntilMs;
    out.right = now < rightUntilMs;
  }

  ISoundBackend& sound() override { return sound_; }
  ISensorBus& sensors() override { return sensors_; }

  void setNow(int64_t nowMs) { nowMs_ = nowMs; }
  int64_t now() const { return nowMs_; }
  SimSensors& simSensors() { return sensors_; }
  uint8_t brightness() const { return brightness_; }

  // Where finished frames go: the terminal renderer or a test hooks this. Unset means nothing is
  // drawn anywhere, which is the normal headless case.
  std::function<void(const Canvas&, uint8_t)> onShow;

  int64_t leftUntilMs = 0;
  int64_t selectUntilMs = 0;
  int64_t rightUntilMs = 0;
  // Same units the ESP32 board reads: raw ADC counts and millivolts at the divider pin.
  int ldrRaw = 1200;
  int batteryPinMillivolts = 2290;

 private:
  MatrixLayout layout_;
  render::ColorGrade grade_;
  std::unique_ptr<Canvas> graded_;
  SimSound sound_;
  SimSensors sensors_;
  uint8_t brightness_ = 120;
  int64_t nowMs_ = 0;
  unsigned long framesShown_ = 0;
};

}
