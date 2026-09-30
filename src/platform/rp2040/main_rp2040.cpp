#include <Arduino.h>
#include <pico/time.h>
#include <pico/rand.h>
#include "core/effects/EffectNoise.h"
#include "system/DeviceServices.h"
#include "system/DevicePageServices.h"
#include "system/ResetReason.h"
#include "platform/rp2040/TimeService.h"
#include "transport/net/DiscoveryService.h"
#include "transport/net/ArtnetService.h"

// Build-only measurement switch; production enables both UDP services.
#ifndef AWTRIX_PICO_UDP
#define AWTRIX_PICO_UDP 1
#endif

#include "AppConfig.h"
#include "platform/BuildFeatures.h"
#include "core/api/CapabilitiesJson.h"
#include "core/CoreEngine.h"
#include "core/FrameClock.h"
#include "core/apps/builtin/DateApp.h"
#include "core/apps/builtin/TimeApp.h"
#include "core/BuiltinCatalog.h"
#include "core/render/PaletteStore.h"
#include "core/render/PowerAnimator.h"
#include "core/render/BootScreen.h"
#include "core/render/ProvisioningScreen.h"
#include "core/render/TextRenderer.h"
#include "transport/net/NetworkService.h"
#include "transport/http/HttpApiServer.h"
#include "system/Log.h"
#include "platform/rp2040/RadioStartup.h"
#include "system/PeripheryService.h"
#include "system/GalacticUnicornControls.h"
#include "core/render/RenderPipeline.h"
#include "hal/BoardRegistry.h"
#include "hal/GalacticUnicornBoard.h"
#include "media/AwtrixFontAdapter.h"
#include "media/DevicePageIcon.h"
#include "persistence/Filesystem.h"
#include "persistence/NvsSettings.h"
#include "persistence/PaletteFiles.h"
#include "persistence/SystemConfigApply.h"
#include "persistence/AppOrderStore.h"
#include <LittleFS.h>

// Feature macros describe this build, not a claim that a runtime service exists.
static_assert(!AWTRIX_FEATURE_SCRIPTING && !AWTRIX_FEATURE_MP3 &&
              !AWTRIX_FEATURE_RADIO && !AWTRIX_FEATURE_OUTBOUND_TLS &&
              !AWTRIX_FEATURE_BROWSER_OTA, "Pico build must not enable unsupported services");

namespace {
using namespace awtrix;

class Display final : public IDisplayService {
 public:
  void sendScreen() override {} // No transport in this phase.
};

IBoard* board;
CoreEngine* engine;
Canvas* canvas;
RenderPipeline* pipeline;
sound::AudioRouter audioRouter; // Null sinks honestly report MP3/radio unavailable.
Display display;
DeviceSystem systemService;
AppRegistry apps;
EffectRegistry effects;
EffectRegistry overlays;
DevicePageClock pageClock;
DevicePageIcon pageIcon;
DevicePageIcon pageIconB;
platform::TimeService timeService;
bool networkWasConnected = false;
#if AWTRIX_PICO_UDP
DiscoveryService discovery;
ArtnetService artnet;
#endif
BuiltinCatalog builtins;
PeripheryService periphery;
GalacticUnicornControls controls;
NetworkService network;
HttpApiServer http;
render::PowerAnimator* powerAnimator;
int64_t nextFrameMs = 0;

awtrix::DeviceConfig config;
bool storageReady = false;
bool settingsDirty = false;
int64_t lastSettingsSaveMs = 0;

bool holdingSelectAtBoot() {
  ButtonState buttons;
  board->pollButtons(buttons);
  if (!buttons.select) return false;
  const unsigned long start = millis();
  while (millis() - start < 1000) {
    board->pollButtons(buttons);
    if (!buttons.select) return false;
    delay(20);
  }
  canvas->clear(0);
  text::drawText(*canvas, awtrixFont(), 0, 6, "SETUP", 0xFFA000u);
  board->show(*canvas);
  Serial.println("boot: SELECT held, forcing provisioning AP (credentials kept)");
  return true;
}
}

void setup() {
  Serial.begin(115200);
  awtrix::noise::reseed(get_rand_32());
  Serial.printf("reset: %s\n", platform::resetReasonName());
  // Never wait for a USB host: the panel must boot with power alone.
  storageReady = awtrix::fs::begin();
  if (!storageReady) Serial.println("storage: mount failed; persistence unavailable");
  config = awtrix::galacticUnicornDefaults();
  if (storageReady) {
    config.load();
    awtrix::palettefiles::install();
  }
  awtrix::sysconfig::adoptFixedBoard(config);
  logbuf::setVerbose(config.debugMode);
  board = &awtrix::activeBoard(config);
  board->begin();
  board->setMatrixLayout(config.matrixLayout());

  canvas = new awtrix::Canvas(board->matrixWidth(), board->matrixHeight());
  systemService.setWakeButtonPin(27);
  systemService.setDisplayOff([] { canvas->clear(0); board->show(*canvas); });
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
  deps.icons = &pageIcon;
  deps.iconsB = &pageIconB;
  deps.fonts[0] = &awtrix::awtrixFont(awtrix::FontId::Small);
  deps.fonts[1] = &awtrix::awtrixFont(awtrix::FontId::Large);
  pipeline = new awtrix::RenderPipeline(board->matrixWidth(), board->matrixHeight(), deps);
  Serial.printf("boot: AWTRIX NG %s on %s (%dx%d); PIO/DMA panel\n",
                AWTRIX_NG_VERSION, board->name(), board->matrixWidth(), board->matrixHeight());
  const int64_t bootT0 = time_us_64() / 1000;
  auto showBootLogo = [bootT0] {
    render::drawBootLogo(*canvas, awtrixFont(), bootT0, time_us_64() / 1000);
    board->show(*canvas);
  };
  showBootLogo();
  const bool forceAp = holdingSelectAtBoot();
  platform::beginRadio();
  network.setStatus(&engine->state().runtime().wifi);
  network.setOnJoinedFromAp([] { systemService.reboot(); });
  network.begin(config, forceAp, showBootLogo);
  timeService.apply(config.tz, config.ntpServer);
  networkWasConnected = network.isConnected();
  String mac = WiFi.macAddress();
  mac.replace(":", "");
  mac.toLowerCase();
  const uint16_t webPort = network.apMode() ? 80 :
      (config.webPort > 0 ? static_cast<uint16_t>(config.webPort) : 80);
  http.begin(webPort, *engine, *board, *canvas, mac.c_str(), config, network.apMode());
  http.setCapabilitiesJson(std::make_shared<const std::string>(api::capabilitiesJson(
      effects.names(), effects.paletteNames(), overlays.names(), audioRouter.caps(),
      platform::buildFeatures())));
  http.setOnConfigChanged([] {
    // The panel size is fixed, so only mirror and rotate reach the board, and they apply live.
    board->setMatrixLayout(config.matrixLayout());
    engine->state().runtime().tempDecimals = config.tempDecimals;
    logbuf::setVerbose(config.debugMode);
    timeService.apply(config.tz, config.ntpServer);
  });
  http.setOnAssetsChanged([] {
    pipeline->invalidateIcons();
    render::clearPaletteCache();
  });
  periphery.setUid(mac.c_str());
#if AWTRIX_PICO_UDP
  if (networkWasConnected) discovery.begin(network.hostname(), config.webPort);
#endif
  static_cast<GalacticUnicornBoard*>(board)->logRefreshProgress();
}

void loop() {
  const int64_t nowMs = static_cast<int64_t>(time_us_64() / 1000);
  network.tick();
  http.tick();
  const bool connected = network.isConnected();
  timeService.apply(config.tz, config.ntpServer, connected && !networkWasConnected);
#if AWTRIX_PICO_UDP
  if (connected && !networkWasConnected) discovery.begin(network.hostname(), config.webPort);
  if (connected && config.artnet) artnet.begin();
  else artnet.end();
  if (connected) discovery.tick();
#endif
  networkWasConnected = connected;
  controls.tick(*engine, static_cast<awtrix::GalacticUnicornBoard*>(board)->readInputs(), nowMs);
  periphery.tick(nowMs);
  if (settingsDirty && storageReady && !systemService.hasPending() && nowMs - lastSettingsSaveMs > 1500) {
    awtrix::nvs::saveSettings(engine->state().settings());
    settingsDirty = false;
    lastSettingsSaveMs = nowMs;
  }
  if (nowMs < nextFrameMs) { delay(1); return; }
  nextFrameMs = nowMs + awtrix::kFramePeriodMs;
  {
    static uint16_t frames = 0;
    static int64_t windowStart = 0;
    ++frames;
    if (nowMs - windowStart >= 1000) {
      engine->state().runtime().fps = frames;
      frames = 0;
      windowStart = nowMs;
    }
  }
  audioRouter.tick(nowMs);
  engine->tick(nowMs);
  const bool wakeNotif = engine->hasNotification() && engine->notifications().current().wakeup;
  switch (powerAnimator->update(!engine->state().runtime().matrixOff || wakeNotif, nowMs)) {
    case awtrix::render::PowerAnimator::Phase::Off: canvas->clear(0); break;
    case awtrix::render::PowerAnimator::Phase::Out: powerAnimator->composeOut(*canvas); break;
    default:
      if (engine->state().runtime().moodlightMode) {
        canvas->clear(engine->state().runtime().moodlightColor);
        board->setBrightness(engine->state().runtime().moodlightBrightness);
      } else if (network.apMode()) {
        render::drawProvisioningScreen(*canvas, awtrixFont(), nowMs);
#if AWTRIX_PICO_UDP
      } else if (artnet.tick(*canvas, nowMs)) {
        // Art-Net owns this frame until the shared five-second hold expires.
#endif
      } else {
        pipeline->renderFrame(*canvas, nowMs);
      }
      powerAnimator->finish(*canvas);
      break;
  }
  const auto& settings = engine->state().settings();
  board->applyColorGrade(awtrix::render::gradeFrom(settings));

  board->show(*canvas);

  if (systemService.hasPending() && !powerAnimator->busy()) {
    if (settingsDirty && storageReady && !systemService.resetsSettings())
      awtrix::nvs::saveSettings(engine->state().settings());
    systemService.runPending();
  }
}
