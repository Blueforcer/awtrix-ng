#pragma once
#include "core/apps/AppRegistry.h"
#include "core/apps/builtin/BatteryApp.h"
#include "core/apps/builtin/DateApp.h"
#include "core/apps/builtin/HumidityApp.h"
#include "core/apps/builtin/TempApp.h"
#include "core/apps/builtin/TimeApp.h"
#include "core/effects/EffectRegistry.h"
#include "core/effects/effects/FadeEffect.h"
#include "core/effects/effects/MoreEffects.h"
#include "core/effects/effects/PlasmaEffect.h"
#include "core/effects/effects/TheaterChaseEffect.h"
#include "core/effects/overlays/RainOverlay.h"
#include "core/effects/overlays/SnowOverlay.h"
#include "core/effects/overlays/WeatherOverlays.h"

namespace awtrix {
// One portable catalog for device platforms. Own it for the registries' lifetime.
// Sensor apps remain registered; CoreEngine gates their presence on sensor availability.
class BuiltinCatalog {
 public:
  void addTo(AppRegistry& apps, EffectRegistry& effects, EffectRegistry& overlays) {
    apps.add(&time_); apps.add(&date_); apps.add(&temp_); apps.add(&humidity_); apps.add(&battery_);
    IEffect* effectList[] = {&plasma_, &chase_, &fade_, &line_, &brick_,
         &ping_, &radar_, &check_, &fire_, &cloud_, &ripple_, &snake_, &pacifica_, &matrix_,
         &swirlIn_, &swirlOut_, &eyes_, &stars_, &waves_};
    for (auto* effect : effectList) effects.add(effect);
    IEffect* overlayList[] = {&rain_, &snow_, &drizzle_, &storm_, &thunder_, &frost_};
    for (auto* overlay : overlayList) overlays.add(overlay);
  }
 private:
  TimeApp time_; DateApp date_; TempApp temp_; HumidityApp humidity_; BatteryApp battery_;
  PlasmaEffect plasma_; TheaterChaseEffect chase_; FadeEffect fade_; MovingLineEffect line_;
  BrickBreakerEffect brick_; PingPongEffect ping_; RadarEffect radar_; CheckerboardEffect check_;
  FireworksEffect fire_; PlasmaCloudEffect cloud_; RippleEffect ripple_; SnakeEffect snake_;
  PacificaEffect pacifica_; MatrixEffect matrix_; SwirlInEffect swirlIn_; SwirlOutEffect swirlOut_;
  LookingEyesEffect eyes_; TwinklingStarsEffect stars_; ColorWavesEffect waves_;
  RainOverlay rain_; SnowOverlay snow_; DrizzleOverlay drizzle_; StormOverlay storm_;
  ThunderOverlay thunder_; FrostOverlay frost_;
};
}
