#pragma once

#include "AppConfig.h"
#include "core/PinRules.h"
#include "core/render/MatrixLayout.h"
#include "hal/IBoard.h"
#include "hal/SensorBus.h"
#include "media/MatrixRenderer.h"
#include "sound/BuzzerBackend.h"
#include "sound/DFPlayerBackend.h"

namespace awtrix {

struct DeviceConfig;

class Esp32Board : public IBoard {
 public:
  explicit Esp32Board(const DeviceConfig& cfg);

  const char* name() const override { return pins::activeProfile().label; }
  int matrixWidth() const override { return layout_.width(); }
  int matrixHeight() const override { return layout_.height(); }

  void begin() override;
  void show(const Canvas& canvas) override { renderer_.show(canvas); }
  void setBrightness(uint8_t brightness) override { renderer_.setBrightness(brightness); }
  void setMatrixLayout(const MatrixLayout& layout) override {
    layout_ = layout;
    renderer_.setLayout(layout);
  }
  void applyColorGrade(const render::GradeParams& grade) override { renderer_.setGrade(grade); }

  // A pin of -1 means "not wired": that is how every optional peripheral is switched off.
  bool hasBattery() const override { return pins_.battery >= 0; }
  bool hasLightSensor() const override { return pins_.ldr >= 0; }
  int readBatteryMillivolts() override;
  int readLdrRaw() override;
  void pollButtons(ButtonState& out) override;

  ISoundBackend& sound() override { return *sound_; }
  ISensorBus& sensors() override { return sensors_; }

 private:
  pins::PinSet pins_;
  bool pinsWereInvalid_ = false;
  bool layoutWasInvalid_ = false;
  MatrixLayout layout_;
  MatrixRenderer renderer_;
  BuzzerBackend buzzer_;
  DFPlayerBackend dfplayer_;
  // Points at the buzzer unless the config wires up and enables a DFPlayer; both stay alive.
  ISoundBackend* sound_ = &buzzer_;
  SensorBus sensors_;
};

}
