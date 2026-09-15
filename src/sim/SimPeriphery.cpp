#include "sim/SimPeriphery.h"

#include "core/Command.h"
#include "core/CoreEngine.h"

namespace awtrix {

namespace {
constexpr long kSensorIntervalMs = 2000;
constexpr long kLdrIntervalMs = 100;
}

LightConfig SimPeriphery::lightConfig() const {
  LightConfig lc;
  lc.factor = cfg_->ldrFactor;
  lc.gamma = cfg_->ldrGamma;
  lc.onGround = cfg_->ldrOnGround;
  lc.minBrightness = cfg_->minBrightness;
  lc.maxBrightness = cfg_->maxBrightness;
  return lc;
}

void SimPeriphery::begin(CoreEngine& engine, IBoard& board, const DeviceConfig& cfg) {
  engine_ = &engine;
  board_ = &board;
  cfg_ = &cfg;
}

void SimPeriphery::tick(int64_t nowMs) {
  ButtonState sample{};
  board_->pollButtons(sample);
  const auto debounce = [&](bool raw, bool& lastRaw, int64_t& changedMs, bool& stable) {
    if (raw != lastRaw) {
      lastRaw = raw;
      changedMs = nowMs;
    }
    if (raw != stable && nowMs - changedMs >= kDebounceMs) stable = raw;
  };
  debounce(sample.left, raw_.left, rawChangeMs_[0], stable_.left);
  debounce(sample.select, raw_.select, rawChangeMs_[1], stable_.select);
  debounce(sample.right, raw_.right, rawChangeMs_[2], stable_.right);
  const ButtonState& cur = stable_;
  const bool blocked = engine_->state().settings().blockNavigation;
  const bool lEdge = cur.left && !prev_.left;
  const bool rEdge = cur.right && !prev_.right;
  const bool sEdge = cur.select && !prev_.select;
  // Rotating the panel by 180 degrees physically swaps left and right, so the user's swap setting
  // has to XOR with it rather than simply override it.
  const bool swapped = cfg_->rotate != cfg_->swapButtons;
  // Scripts get first refusal on every press, button by button; returning true suppresses the
  // built-in navigation for that button alone.
  bool tookLeft = false, tookSelect = false, tookRight = false;
  if (buttonHook_) {
    if (lEdge) tookLeft = buttonHook_(swapped ? 2 : 0);
    if (sEdge) tookSelect = buttonHook_(1);
    if (rEdge) tookRight = buttonHook_(swapped ? 0 : 2);
  }
  if (lEdge && !tookLeft && !blocked)
    engine_->submit(Command(swapped ? CommandType::NextApp : CommandType::PreviousApp));
  if (rEdge && !tookRight && !blocked)
    engine_->submit(Command(swapped ? CommandType::PreviousApp : CommandType::NextApp));
  // One press dismisses the current notification, two inside kDoublePressMs toggle the panel. A
  // consumed press is not remembered, so it can never pair up with the next one.
  if (sEdge && !tookSelect) {
    engine_->submit(Command(CommandType::DismissNotify));
    if (!blocked && nowMs - lastSelectEdgeMs_ <= kDoublePressMs) {
      Command c(CommandType::SetDisplay);
      c.payload = engine_->state().runtime().matrixOff ? "{\"power\":true}" : "{\"power\":false}";
      engine_->submit(c);
    }
    lastSelectEdgeMs_ = nowMs;
  }
  if (cur.left != prev_.left || cur.select != prev_.select || cur.right != prev_.right) {
    engine_->state().runtime().buttons = {cur.left, cur.select, cur.right};
    engine_->state().emit(StateEvent::ButtonsChanged);
  }
  prev_ = cur;

  RuntimeState& rt = engine_->state().runtime();
  const Settings& s = engine_->state().settings();

  if (nowMs - lastLdrMs_ >= kLdrIntervalMs) {
    lastLdrMs_ = nowMs;
    int ldr = board_->readLdrRaw();
    if (ldr < 0) ldr = 0;
    rt.ldrRaw = static_cast<uint16_t>(ldr);
    rt.lightLevel = lightLevelFromRaw(rt.ldrRaw, lightConfig());

    uint8_t bri;
    if (s.autoBrightness) {
      bri = brightnessFromLightLevel(rt.lightLevel, lightConfig());
    } else {
      bri = static_cast<uint8_t>(s.brightness < 0 ? 0 : (s.brightness > 255 ? 255 : s.brightness));
    }
    rt.brightnessActual = bri;
    board_->setBrightness(bri);
  }

  if (nowMs - lastSensorMs_ < kSensorIntervalMs) return;
  lastSensorMs_ = nowMs;

  if (board_->hasBattery()) {
    const int mv = board_->readBatteryMillivolts();
    if (mv >= 0) {
      rt.batteryPinMillivolts = static_cast<uint16_t>(mv);
      rt.batteryVoltage = cellVoltsFromPinMillivolts(mv, cfg_->batteryDividerRatio);
      rt.batteryPercent = socFromVolts(rt.batteryVoltage);
      rt.lowBattery = cfg_->lowBatteryThreshold > 0 && rt.batteryPercent < cfg_->lowBatteryThreshold;
    }
  }

  const SensorReading sr = board_->sensors().read();
  if (sr.present) {
    rt.temperatureC = sr.temperatureC + cfg_->tempOffset;
    if (sr.hasHumidity) rt.humidity = sr.humidity + cfg_->humOffset;
    if (sr.hasPressure) rt.pressureHpa = sr.pressureHpa;
  }
}

}
