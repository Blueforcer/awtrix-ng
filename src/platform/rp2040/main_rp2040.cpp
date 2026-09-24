#include <Arduino.h>
#include <pico/time.h>

#include "AppConfig.h"
#include "platform/BuildFeatures.h"
#include "core/api/CapabilitiesJson.h"
#include "core/CoreEngine.h"
#include "core/FrameClock.h"
#include "core/apps/builtin/DateApp.h"
#include "core/apps/builtin/TimeApp.h"
#include "core/BuiltinCatalog.h"
#include "core/render/PowerAnimator.h"
#include "system/Log.h"
#include "system/PeripheryService.h"
#include "system/GalacticUnicornControls.h"
#include "core/render/RenderPipeline.h"
#include "hal/BoardRegistry.h"
#include "hal/GalacticUnicornBoard.h"
#include "media/AwtrixFontAdapter.h"
#include "persistence/Filesystem.h"
#include "persistence/NvsSettings.h"
#include "persistence/PaletteFiles.h"
#include "persistence/AppOrderStore.h"
#include <LittleFS.h>

// Feature macros describe this build, not a claim that a runtime service exists.
static_assert(!AWTRIX_FEATURE_SCRIPTING && !AWTRIX_FEATURE_MP3 &&
              !AWTRIX_FEATURE_RADIO && !AWTRIX_FEATURE_OUTBOUND_TLS &&
              !AWTRIX_FEATURE_BROWSER_OTA, "Pico build must not enable unsupported services");

namespace {
using namespace awtrix;
class UnsetClock final : public IPageClock {
 public:
  void fill(RenderCtx& ctx, int64_t nowMs) override {
    ctx.nowMs = nowMs;
    ctx.epochMs = -1; // No NTP/RTC yet; built-ins render their unset-clock state.
  }
};
class Display final : public IDisplayService {
 public:
  void sendScreen() override {} // No transport in this phase.
};
class System final : public ISystemService {
 public:
  void reboot() override { rebootPending = true; }
  void sleep(uint64_t) override { Serial.println("sleep unavailable"); }
  void factoryReset() override {
    LittleFS.remove("/NVS/awtrix-cfg.bin");
    resetSettings();
  }
  void resetSettings() override {
    LittleFS.remove("/NVS/awtrix-ng.bin");
    resetPending = true;
    rebootPending = true;
  }
  bool rebootPending = false;
  bool resetPending = false;
};
IBoard* board;
CoreEngine* engine;
Canvas* canvas;
RenderPipeline* pipeline;
sound::AudioRouter audioRouter; // Null sinks honestly report MP3/radio unavailable.
Display display;
System systemService;
AppRegistry apps;
EffectRegistry effects;
EffectRegistry overlays;
UnsetClock pageClock;
BuiltinCatalog builtins;
PeripheryService periphery;
GalacticUnicornControls controls;
render::PowerAnimator* powerAnimator;
int64_t nextFrameMs = 0;

awtrix::DeviceConfig config;
bool storageReady = false;
bool settingsDirty = false;
int64_t lastSettingsSaveMs = 0;
}

void setup() {
  Serial.begin(115200);
  // Never wait for a USB host: the panel must boot with power alone.
  storageReady = awtrix::fs::begin();
  if (!storageReady) Serial.println("storage: mount failed; persistence unavailable");
  config = awtrix::galacticUnicornDefaults();
  if (storageReady) {
    config.load();
    awtrix::palettefiles::install();
  }
  logbuf::setVerbose(config.debugMode);
  board = &awtrix::activeBoard(config);
  board->begin();
  board->setMatrixLayout(config.matrixLayout());

  canvas = new awtrix::Canvas(board->matrixWidth(), board->matrixHeight());
  engine = new awtrix::CoreEngine(audioRouter, display, systemService);
  if (storageReady) {
    awtrix::nvs::loadSettings(engine->state().settings());
    awtrix::apporder::load(*engine);
    engine->setOrderPersist(awtrix::apporder::save);
    engine->state().subscribe([](awtrix::StateEvent event) {
      if (event == awtrix::StateEvent::SettingsChanged) settingsDirty = true;
    });
  }
  engine->state().runtime().tempDecimals = config.tempDecimals;
  engine->setBatteryAvailable(false);
  engine->setTemperatureAvailable(false);
  engine->setHumidityAvailable(false);
  engine->setPressureAvailable(false);
  engine->setLightSensorAvailable(board->hasLightSensor());
  // No script service is installed: the existing dispatcher returns Unavailable.
  builtins.addTo(apps, effects, overlays);
  engine->setEffectRegistry(&effects);
  engine->setOverlayRegistry(&overlays);
  Serial.println(awtrix::api::capabilitiesJson(effects.names(), effects.paletteNames(),
                  overlays.names(), audioRouter.caps(), awtrix::platform::buildFeatures()).c_str());
  periphery.begin(*engine, *board, config);
  powerAnimator = new render::PowerAnimator(board->matrixWidth(), board->matrixHeight());
  awtrix::RenderPipelineDeps deps;
  deps.engine = engine;
  deps.apps = &apps;
  deps.audio = &audioRouter;
  deps.effects = &effects;
  deps.overlays = &overlays;
  deps.clock = &pageClock;
  deps.fonts[0] = &awtrix::awtrixFont(awtrix::FontId::Small);
  deps.fonts[1] = &awtrix::awtrixFont(awtrix::FontId::Large);
  pipeline = new awtrix::RenderPipeline(board->matrixWidth(), board->matrixHeight(), deps);
  Serial.printf("boot: AWTRIX NG %s on %s (%dx%d); PIO/DMA panel, no network yet\n",
                AWTRIX_NG_VERSION, board->name(), board->matrixWidth(), board->matrixHeight());
}

void loop() {
  const int64_t nowMs = static_cast<int64_t>(time_us_64() / 1000);
  controls.tick(*engine, static_cast<awtrix::GalacticUnicornBoard*>(board)->readInputs(), nowMs);
  periphery.tick(nowMs);
  if (settingsDirty && storageReady && !systemService.rebootPending && nowMs - lastSettingsSaveMs > 1500) {
    awtrix::nvs::saveSettings(engine->state().settings());
    settingsDirty = false;
    lastSettingsSaveMs = nowMs;
  }
  if (nowMs < nextFrameMs) { delay(1); return; }
  nextFrameMs = nowMs + awtrix::kFramePeriodMs;
  engine->tick(nowMs);
  audioRouter.tick(nowMs);
  const bool wakeNotif = engine->hasNotification() && engine->notifications().current().wakeup;
  switch (powerAnimator->update(!engine->state().runtime().matrixOff || wakeNotif, nowMs)) {
    case awtrix::render::PowerAnimator::Phase::Off: canvas->clear(0); break;
    case awtrix::render::PowerAnimator::Phase::Out: powerAnimator->composeOut(*canvas); break;
    default:
      pipeline->renderFrame(*canvas, nowMs);
      powerAnimator->finish(*canvas);
      break;
  }
  const auto& settings = engine->state().settings();
  board->applyColorGrade(awtrix::render::gradeFrom(settings));

  board->show(*canvas);

  if (systemService.rebootPending) {
    if (settingsDirty && storageReady && !systemService.resetPending)
      awtrix::nvs::saveSettings(engine->state().settings());
    rp2040.reboot();
  }
}
