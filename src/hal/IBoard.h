#pragma once

#include <cstdint>

#include "core/render/Canvas.h"
#include "core/render/ColorGrade.h"
#include "core/render/MatrixLayout.h"
#include "core/sound/AudioSinks.h"
#include "hal/ISensorBus.h"

namespace awtrix {

struct ButtonState {
  bool left = false;
  bool select = false;
  bool right = false;
};

class IBoard {
 public:
  virtual ~IBoard() = default;

  virtual const char* name() const = 0;
  virtual int matrixWidth() const = 0;
  virtual int matrixHeight() const = 0;

  virtual void begin() = 0;
  virtual void show(const Canvas& canvas) = 0;
  virtual void setBrightness(uint8_t brightness) = 0;
  virtual void setMatrixLayout(const MatrixLayout& layout) = 0;
  virtual void applyColorGrade(const render::GradeParams& grade) = 0;

  virtual bool hasBattery() const = 0;
  virtual bool hasLightSensor() const = 0;
  // Millivolts measured at the divider pin, -1 when no battery is wired.
  virtual int readBatteryMillivolts() = 0;
  // Raw ADC counts, not lux; the light curve is applied further up in AutoBrightness.
  virtual int readLdrRaw() = 0;
  virtual void pollButtons(ButtonState& out) = 0;

  // Null means "not wired on this board". Both can be live at once: LEDC and a UART share nothing,
  // and adding a DFPlayer must not cost the buzzer.
  virtual sound::IToneSink* toneSink() = 0;
  virtual sound::ITrackSink* trackSink() = 0;
  virtual ISensorBus& sensors() = 0;
};

}
