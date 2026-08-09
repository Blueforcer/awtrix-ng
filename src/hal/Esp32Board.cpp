#include "hal/Esp32Board.h"

#include <Arduino.h>

#include "persistence/DeviceConfig.h"
#include "system/Log.h"

namespace awtrix {

// A bad stored pin map or panel layout falls back to the board defaults rather than refusing to
// boot; the warning waits for begin() because the log is not up yet when the board is constructed.
Esp32Board::Esp32Board(const DeviceConfig& cfg) {
  std::string err;
  pins_ = cfg.pinSet();
  if (!pins::validate(pins_, err)) {
    pins_ = pins::activeProfile().defaults;
    pinsWereInvalid_ = true;
  }
  layout_ = sanitizeMatrixLayout(cfg.matrixLayout(), &layoutWasInvalid_);
  buzzer_.setPin(pins_.buzzer);
  dfplayer_.setPins(pins_.dfRx, pins_.dfTx);
  dfplayerWired_ = pins_.dfplayerEnabled && pins_.dfRx >= 0 && pins_.dfTx >= 0;
}

void Esp32Board::begin() {
  if (pinsWereInvalid_)
    logf("gpio: stored pin map invalid, using %s defaults", pins::activeProfile().label);
  if (layoutWasInvalid_)
    logf("matrix: stored panel layout invalid, using the %dx%d default", layout_.width(),
         layout_.height());
  if (pins_.btnLeft >= 0) pinMode(pins_.btnLeft, INPUT_PULLUP);
  if (pins_.btnSelect >= 0) pinMode(pins_.btnSelect, INPUT_PULLUP);
  if (pins_.btnRight >= 0) pinMode(pins_.btnRight, INPUT_PULLUP);
  // 120 is only the brightness for the first few frames; the periphery loop takes over immediately.
  renderer_.begin(pins_.matrix, layout_, 120);
  // Both: LEDC and the UART share nothing, so a DFPlayer costs no melodies.
  if (pins_.buzzer >= 0) buzzer_.begin();
  if (dfplayerWired_) dfplayer_.begin();
  sensors_.setPins(pins_.i2cSda, pins_.i2cScl);
  sensors_.begin();
}

int Esp32Board::readBatteryMillivolts() {
  return pins_.battery >= 0 ? static_cast<int>(analogReadMilliVolts(pins_.battery)) : -1;
}
int Esp32Board::readLdrRaw() { return pins_.ldr >= 0 ? analogRead(pins_.ldr) : 0; }

// Buttons pull to ground against the internal pull-up, so LOW is pressed.
void Esp32Board::pollButtons(ButtonState& out) {
  out.left = pins_.btnLeft >= 0 && digitalRead(pins_.btnLeft) == LOW;
  out.select = pins_.btnSelect >= 0 && digitalRead(pins_.btnSelect) == LOW;
  out.right = pins_.btnRight >= 0 && digitalRead(pins_.btnRight) == LOW;
}

}
