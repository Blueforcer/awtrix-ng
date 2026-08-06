#include "system/PeripheryService.h"

#include <Arduino.h>
#include <HTTPClient.h>
#include <WiFiClient.h>

#include <cmath>

#include "core/Command.h"
#include "core/CoreEngine.h"

namespace awtrix {

namespace {
constexpr long kSensorIntervalMs = 2000;
constexpr long kLdrIntervalMs = 100;

// Fires from the main loop with very short timeouts: a callback host that is down or slow would
// otherwise freeze the display for the length of a TCP connect.
void postButton(const std::string& url, const char* btn, bool state, const std::string& uid) {
  WiFiClient wc;
  HTTPClient http;
  http.setConnectTimeout(300);
  http.setTimeout(300);
  if (!http.begin(wc, url.c_str())) return;
  http.addHeader("Content-Type", "application/x-www-form-urlencoded");
  String body = String("button=") + btn + "&state=" + (state ? "1" : "0") + "&uid=" + uid.c_str();
  http.POST(body);
  http.end();
}

}

LightConfig PeripheryService::lightConfig() const {
  LightConfig lc;
  lc.factor = cfg_->ldrFactor;
  lc.gamma = cfg_->ldrGamma;
  lc.onGround = cfg_->ldrOnGround;
  lc.minBrightness = cfg_->minBrightness;
  lc.maxBrightness = cfg_->maxBrightness;
  return lc;
}

void PeripheryService::begin(CoreEngine& engine, IBoard& board, const DeviceConfig& cfg) {
  engine_ = &engine;
  board_ = &board;
  cfg_ = &cfg;
}

void PeripheryService::tick(int64_t nowMs) {
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
  // Rotating the panel puts the buttons on the other side, so it flips left and right the same way
  // the explicit swap setting does; setting both cancels out.
  const bool swapped = cfg_->rotate != cfg_->swapButtons;
  // Scripts get first refusal on every press; only what they do not take reaches the built-in
  // navigation.
  bool consumed = false;
  if (buttonHook_) {
    if (lEdge) consumed |= buttonHook_(swapped ? 2 : 0);
    if (sEdge) consumed |= buttonHook_(1);
    if (rEdge) consumed |= buttonHook_(swapped ? 0 : 2);
  }
  if (!consumed) {
    if (lEdge && !blocked)
      engine_->submit(Command(swapped ? CommandType::NextApp : CommandType::PreviousApp));
    if (rEdge && !blocked)
      engine_->submit(Command(swapped ? CommandType::PreviousApp : CommandType::NextApp));
    if (sEdge) {
      engine_->submit(Command(CommandType::DismissNotify));
      if (!blocked && nowMs - lastSelectEdgeMs_ <= kDoublePressMs) {
        Command c(CommandType::SetDisplay);
        c.payload = engine_->state().runtime().matrixOff ? "{\"power\":true}" : "{\"power\":false}";
        engine_->submit(c);
      }
      lastSelectEdgeMs_ = nowMs;
    }
  }
  if (cur.left != prev_.left || cur.select != prev_.select || cur.right != prev_.right) {
    engine_->state().runtime().buttons = {cur.left, cur.select, cur.right};
    engine_->state().emit(StateEvent::ButtonsChanged);
  }
  if (!cfg_->buttonCallback.empty()) {
    if (cur.left != prev_.left) postButton(cfg_->buttonCallback, "left", cur.left, uid_);
    if (cur.select != prev_.select) postButton(cfg_->buttonCallback, "middle", cur.select, uid_);
    if (cur.right != prev_.right) postButton(cfg_->buttonCallback, "right", cur.right, uid_);
  }
  prev_ = cur;

  RuntimeState& rt = engine_->state().runtime();
  const Settings& s = engine_->state().settings();

  if (nowMs - lastLdrMs_ >= kLdrIntervalMs) {
    lastLdrMs_ = nowMs;
    int ldr = board_->readLdrRaw();
    if (ldr < 0) ldr = 0;
    const uint16_t med = ldrFilter_.push(static_cast<uint16_t>(ldr));
    rt.ldrRaw = med;
    rt.lightLevel = lightLevelFromRaw(med, lightConfig());

    uint8_t bri;
    if (s.autoBrightness) {
      bri = brightnessFromLightLevel(rt.lightLevel, lightConfig());
      briSmoother_.setTimeConstant(cfg_->brightnessSmoothing);
      // Coming out of manual brightness, seed the smoother instead of letting it ramp from
      // whatever value it was last left at.
      if (!wasAutoBrightness_) briSmoother_.reset(bri);
      bri = briSmoother_.apply(bri, kLdrIntervalMs);
    } else {
      bri = static_cast<uint8_t>(s.brightness < 0 ? 0 : (s.brightness > 255 ? 255 : s.brightness));
      briSmoother_.reset(bri);
    }
    wasAutoBrightness_ = s.autoBrightness;
    rt.brightnessActual = bri;
    board_->setBrightness(bri);
  }

  if (nowMs - lastSensorMs_ < kSensorIntervalMs) return;
  lastSensorMs_ = nowMs;

  if (board_->hasBattery()) {
    const int mv = board_->readBatteryMillivolts();
    if (mv >= 0) {
      const uint16_t medMv = batteryFilter_.push(static_cast<uint16_t>(mv));
      rt.batteryPinMillivolts = medMv;
      rt.batteryVoltage = cellVoltsFromPinMillivolts(medMv, cfg_->batteryDividerRatio);
      rt.batteryPercent = socFromVolts(rt.batteryVoltage);
      rt.lowBattery = cfg_->lowBatteryThreshold > 0 && rt.batteryPercent < cfg_->lowBatteryThreshold;
    }
  }

  const SensorReading sr = board_->sensors().read();
  if (sr.present) {
    if (std::isfinite(sr.temperatureC)) rt.temperatureC = sr.temperatureC + cfg_->tempOffset;
    if (sr.hasHumidity && std::isfinite(sr.humidity)) rt.humidity = sr.humidity + cfg_->humOffset;
    if (sr.hasPressure && std::isfinite(sr.pressureHpa)) rt.pressureHpa = sr.pressureHpa;
  }
}

}
