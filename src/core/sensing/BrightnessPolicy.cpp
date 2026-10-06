#include "core/sensing/BrightnessPolicy.h"

#include "core/RuntimeState.h"
#include "core/Settings.h"

namespace awtrix {

uint8_t BrightnessPolicy::resolve(const Settings& settings, const RuntimeState& runtime,
                                  const LightConfig& light, int64_t nowMs) {
  const int64_t elapsedMs = primed_ ? nowMs - lastMs_ : 0;
  lastMs_ = nowMs;
  primed_ = true;

  // The sensor is followed under a moodlight too.
  const bool followLight = settings.autoBrightness && runtime.hasLightSensor;
  if (followLight) {
    const uint8_t target = brightnessFromLightLevel(runtime.lightLevel, light);
    smoother_.setTimeConstant(light.smoothingMs);
    if (!autoActive_ || light.smoothingMs <= 0) {
      smoother_.reset(target);
      autoLevel_ = target;
    } else if (elapsedMs > 0) {
      autoLevel_ = smoother_.apply(target, static_cast<long>(elapsedMs));
    }
  }
  autoActive_ = followLight;

  if (runtime.moodlightMode) return runtime.moodlightBrightness;
  if (followLight) return autoLevel_;
  const int manual = settings.brightness;
  return static_cast<uint8_t>(manual < 0 ? 0 : (manual > 255 ? 255 : manual));
}

}
