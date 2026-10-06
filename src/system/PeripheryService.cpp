#include "system/PeripheryService.h"
#include "core/sensing/BatteryModel.h"

#include <Arduino.h>
#include <HTTPClient.h>
#include <WiFiClient.h>

#include <cmath>

#include "core/CoreEngine.h"
#include "core/input/ButtonWebhook.h"

namespace awtrix {

namespace {
constexpr long kSensorIntervalMs = 2000;
constexpr long kLdrIntervalMs = 100;

// Fires from the main loop with very short timeouts: a callback host that is down or slow would
// otherwise freeze the display for the length of a TCP connect.
void postButton(const std::string& url, int button, bool state, const std::string& uid) {
  WiFiClient wc;
  HTTPClient http;
  http.setConnectTimeout(300);
  http.setTimeout(300);
  if (!http.begin(wc, url.c_str())) return;
  http.addHeader("Content-Type", "application/json");
  const std::string body = input::webhookPressBody(input::kWebhookButtonNames[button], state, uid);
  http.POST(reinterpret_cast<uint8_t*>(const_cast<char*>(body.data())), body.size());
  http.end();
}

}

void PeripheryService::begin(CoreEngine& engine, IBoard& board, const DeviceConfig& cfg,
                             input::IButtonMenu* menu) {
  engine_ = &engine;
  board_ = &board;
  buttons_.begin(engine, cfg, menu);
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
  const DeviceConfig& cfg = buttons_.config();
  buttons_.setState(stable_, nowMs, [&](int button, bool pressed) {
    if (!cfg.buttonCallback.empty()) postButton(cfg.buttonCallback, button, pressed, uid_);
  });

  RuntimeState& rt = engine_->state().runtime();

  if (board_->hasLightSensor() && nowMs - lastLdrMs_ >= kLdrIntervalMs) {
    lastLdrMs_ = nowMs;
    int ldr = board_->readLdrRaw();
    if (ldr < 0) ldr = 0;
    const uint16_t med = ldrFilter_.push(static_cast<uint16_t>(ldr));
    rt.ldrRaw = med;
    rt.lightLevel = lightLevelFromRaw(med, cfg.lightConfig());
  }

  if (nowMs - lastSensorMs_ < kSensorIntervalMs) return;
  lastSensorMs_ = nowMs;

  if (board_->hasBattery()) {
    const int mv = board_->readBatteryMillivolts();
    if (mv >= 0) {
      const uint16_t medMv = batteryFilter_.push(static_cast<uint16_t>(mv));
      rt.batteryPinMillivolts = medMv;
      rt.batteryVoltage = cellVoltsFromPinMillivolts(medMv, cfg.batteryDividerRatio);
      rt.batteryPercent = socFromVolts(rt.batteryVoltage);
      rt.lowBattery = isLowBattery(rt.batteryPercent, cfg.lowBatteryThreshold);
    }
  }

  const SensorReading sr = board_->sensors().read();
  if (sr.present) {
    if (std::isfinite(sr.temperatureC)) rt.temperatureC = sr.temperatureC + cfg.tempOffset;
    if (sr.hasHumidity && std::isfinite(sr.humidity)) rt.humidity = sr.humidity + cfg.humOffset;
    if (sr.hasPressure && std::isfinite(sr.pressureHpa)) rt.pressureHpa = sr.pressureHpa;
  }
}

}
