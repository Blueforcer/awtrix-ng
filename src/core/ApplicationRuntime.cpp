#include "core/ApplicationRuntime.h"

#include <utility>

#include "core/effects/effects/FadeEffect.h"
#include "core/effects/effects/MoreEffects.h"
#include "core/effects/effects/PlasmaEffect.h"
#include "core/effects/effects/SimulatedEffects.h"
#include "core/effects/effects/TheaterChaseEffect.h"
#include "core/effects/overlays/RainOverlay.h"
#include "core/effects/overlays/SnowOverlay.h"
#include "core/effects/overlays/WeatherOverlays.h"
#include "core/script/ScriptHost.h"
#include "core/sound/AudioSettings.h"
#include "hal/IBoard.h"

namespace awtrix {

struct ApplicationRuntime::Effects {
  PlasmaEffect plasma;
  TheaterChaseEffect theater;
  FadeEffect fade;
  MovingLineEffect line;
  BrickBreakerEffect brick;
  PingPongEffect pingPong;
  RadarEffect radar;
  CheckerboardEffect checker;
  FireworksEffect fireworks;
  PlasmaCloudEffect cloud;
  RippleEffect ripple;
  SnakeEffect snake;
  PacificaEffect pacifica;
  MatrixEffect matrix;
  SwirlInEffect swirlIn;
  SwirlOutEffect swirlOut;
  LookingEyesEffect eyes;
  TwinklingStarsEffect stars;
  ColorWavesEffect waves;
  RainOverlay rain;
  SnowOverlay snow;
  DrizzleOverlay drizzle;
  StormOverlay storm;
  ThunderOverlay thunder;
  FrostOverlay frost;

  void install(EffectRegistry& effects, EffectRegistry& overlays) {
    IEffect* effectList[] = {&plasma, &theater, &fade, &line, &brick,
         &pingPong, &radar, &checker, &fireworks, &cloud, &ripple, &snake, &pacifica, &matrix,
         &swirlIn, &swirlOut, &eyes, &stars, &waves};
    IEffect* overlayList[] = {&rain, &snow, &drizzle, &storm, &thunder, &frost};
    for (IEffect* effect : effectList) effects.add(effect);
    for (IEffect* overlay : overlayList) overlays.add(overlay);
  }
};

ApplicationRuntime::ApplicationRuntime(int width, int height, ApplicationServices services,
                                       AppRegistry apps)
    : services_(std::move(services)), ownedEffects_(new Effects), apps_(std::move(apps)),
      engine_(services_.audio, services_.display, services_.system, apps_.ids()), canvas_(width, height),
      power_(width, height), pipeline_(width, height, renderDeps()) {
  ownedEffects_->install(effects_, overlays_);
  engine_.setEffectRegistry(&effects_);
  engine_.setOverlayRegistry(&overlays_);
}

ApplicationRuntime::~ApplicationRuntime() = default;

void ApplicationRuntime::markSensors(IBoard& board) {
  engine_.setBatteryAvailable(board.hasBattery());
  engine_.setLightSensorAvailable(board.hasLightSensor());
  engine_.setTemperatureAvailable(board.sensors().hasSensor());
  engine_.setHumidityAvailable(board.sensors().hasHumidity());
  engine_.setPressureAvailable(board.sensors().hasPressure());
}

std::string ApplicationRuntime::capabilitiesJson(const PlatformDescriptor& platform,
                                                const api::CapabilityMembersFn& members) const {
  return api::capabilitiesJson(effects_.names(), effects_.paletteNames(), overlays_.names(),
                               services_.audio.caps(), &platform, &services_.fonts, members);
}

void ApplicationRuntime::applyOutputs(IBoard& board) {
  const Settings& settings = engine_.state().settings();
  board.applyColorGrade(render::gradeFrom(settings));
  applyAudioSettings(services_.audio, settings);
}

void ApplicationRuntime::bindOutputs(IBoard& board, bool& settingsDirty) {
  engine_.state().subscribe([this, &board, &settingsDirty](StateEvent event) {
    if (event != StateEvent::SettingsChanged) return;
    applyOutputs(board);
    settingsDirty = true;
  });
}

void ApplicationRuntime::configureScripts(script::ScriptServices& services) {
  services.effects = &effects_;
  services.overlays = &overlays_;
  services.panel = &canvas_;
  services.fonts = &services_.fonts;
  services.application = &scriptApplication_;
}

RenderPipelineDeps ApplicationRuntime::renderDeps() {
  RenderPipelineDeps deps;
  deps.engine = &engine_;
  deps.apps = &apps_;
  deps.effects = &effects_;
  deps.overlays = &overlays_;
  deps.fonts = &services_.fonts;
  deps.icons = services_.icons;
  deps.iconsB = services_.iconsB;
  deps.audio = &services_.audio;
  deps.clock = &services_.clock;
  deps.external = services_.externalPage;
  deps.zoom = services_.pageZoom;
  return deps;
}

void ApplicationRuntime::publishContent(const PageInfo* page, int64_t nowMs) {
  if (services_.contentSink) services_.contentSink->content(canvas_, page, nowMs);
}

void ApplicationRuntime::beginFrame(int64_t nowMs) {
  ++frames_;
  if (nowMs - windowStartMs_ >= 1000) {
    engine_.state().runtime().fps = frames_;
    frames_ = 0;
    windowStartMs_ = nowMs;
  }
  updateBrightness(nowMs);
}

// Runs from beginFrame() and render().
void ApplicationRuntime::updateBrightness(int64_t nowMs) {
  RuntimeState& runtime = engine_.state().runtime();
  const LightConfig light = services_.lightConfig ? services_.lightConfig() : LightConfig{};
  const uint8_t level = brightness_.resolve(engine_.state().settings(), runtime, light, nowMs);
  runtime.brightnessActual = level;
  if (level == applied_) return;
  applied_ = level;
  if (services_.setBrightness) services_.setBrightness(level);
}

void ApplicationRuntime::tick(int64_t nowMs, script::ScriptHost* scripts, TickObserver observer) {
  services_.audio.tick(nowMs);
  if (services_.audio.takeStatusChanged()) engine_.state().emit(StateEvent::AudioChanged);
  if (observer) observer(ApplicationTickStage::Audio);
  engine_.tick(nowMs);
  launcher_.tick(nowMs);
  if (observer) observer(ApplicationTickStage::Engine);
  RenderCtx ctx;
  ctx.settings = &engine_.state().settings();
  ctx.runtime = &engine_.state().runtime();
  ctx.font = services_.fonts.small().font;
  ctx.fonts = &services_.fonts;
  services_.clock.fill(ctx, nowMs);
  if (scripts) scripts->tick(ctx, engine_.currentAppId(), engine_.incomingAppId());
  if (observer) observer(ApplicationTickStage::Scripts);
}

bool ApplicationRuntime::render(int64_t nowMs, FrameOverride platformFrame, void* context) {
  // Without a canvas nothing renders; services keep running.
  if (!canvas_.valid()) return false;
  updateBrightness(nowMs);
  const RuntimeState& state = engine_.state().runtime();
  const bool wake = engine_.hasNotification() && engine_.notifications().current().wakeup;
  switch (power_.update(!state.matrixOff || wake, nowMs)) {
    case render::PowerAnimator::Phase::Off:
      canvas_.clear(0x000000u);
      pipeline_.skipFrame();
      publishContent(nullptr, nowMs);
      return false;
    case render::PowerAnimator::Phase::Out:
      power_.composeOut(canvas_);
      pipeline_.skipFrame();
      publishContent(nullptr, nowMs);
      return false;
    default:
      break;
  }
  PlatformFrame source = PlatformFrame::None;
  const PageInfo* page = nullptr;
  if (state.moodlightMode) {
    canvas_.clear(state.moodlightColor);
    pipeline_.skipFrame();
  } else {
    if (platformFrame) source = platformFrame(canvas_, nowMs, context);
    if (source == PlatformFrame::None) {
      pipeline_.renderFrame(canvas_, nowMs);
      page = &pipeline_.shownPage();
    } else {
      pipeline_.skipFrame();
    }
  }
  publishContent(page, nowMs);
  launcherView_.draw(canvas_, launcher_, *services_.fonts.small().font, nowMs);
  power_.finish(canvas_);
  return source == PlatformFrame::ExternalRate;
}

}
