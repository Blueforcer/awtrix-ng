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
#include "transport/mqtt/MqttService.h"

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
#include "system/PeripheryHttp.h"
#include "system/PeripheryService.h"
#include "system/Watchdog.h"
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
#if AWTRIX_FEATURE_SCRIPTING
#include "core/script/ScriptHeap.h"
#include "core/script/ScriptHost.h"
#include "core/script/ScriptService.h"
#include "core/script/ScriptSoundCommand.h"
#include "core/script/ScriptSourceService.h"
#include "persistence/ScriptStore.h"
#include "platform/rp2040/ScriptHeapRp2040.h"
#include "system/MonotonicClock.h"
#include "transport/ScriptMqttBridge.h"
#endif

// Feature macros describe this build, not a claim that a runtime service exists. Scripting is on
// unless build_flags turn it off; the rest stay out.
static_assert(!AWTRIX_FEATURE_MP3 &&
              !AWTRIX_FEATURE_RADIO && !AWTRIX_FEATURE_OUTBOUND_TLS &&
              !AWTRIX_FEATURE_BROWSER_OTA, "Pico build must not enable unsupported services");

namespace {
using namespace awtrix;

IBoard* board;
CoreEngine* engine;
Canvas* canvas;
RenderPipeline* pipeline;
sound::AudioRouter audioRouter; // Null sinks honestly report MP3/radio unavailable.
DeviceDisplay display;
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
MqttService mqtt;
std::unique_ptr<net::IHostResolver> mqttResolver;
render::PowerAnimator* powerAnimator;
int64_t nextFrameMs = 0;

awtrix::DeviceConfig config;
bool storageReady = false;
bool settingsDirty = false;
int64_t lastSettingsSaveMs = 0;
#if AWTRIX_FEATURE_SCRIPTING
ScriptMqttBridge scriptMqtt;
ScriptStore scriptStore;
script::ScriptServices scriptSvc;
script::ScriptHost* scripts = nullptr;
#endif

#if AWTRIX_FEATURE_SCRIPTING
// The same wiring as the ESP32's main.cpp, less what the Pico does not have: no HTTP requests
// (ScriptHttpWorker is ESP32-only) and no icons (ScriptIcon), so scripts that ask get "false".
// Every call into the VM is bounded by BerryVM::kInstructionLimit and returns to loop(), which
// feeds the watchdog.
void beginScripting() {
  scriptSvc.mqtt = &scriptMqtt;
  scriptSvc.storeSink = &scriptStore;
  scriptSvc.effects = &effects;
  scriptSvc.overlays = &overlays;
  scriptSvc.notify = [](const std::string& json) {
    DispatchDetail detail;
    return engine->notify(json, static_cast<uint8_t>(Source::Internal), detail) ==
           DispatchResult::Ok;
  };
  scriptSvc.settings = [] { return &engine->state().settings(); };
  scriptSvc.runtime = [] { return &engine->state().runtime(); };
  scriptSvc.fonts[0] = &awtrixFont(FontId::Small);
  scriptSvc.fonts[1] = &awtrixFont(FontId::Large);
  scriptSvc.panel = canvas;
  scriptSvc.setSettings = [](const std::string& json) {
    Command c(CommandType::SetSettings);
    c.payload = json;
    c.source = Source::Internal;
    return engine->submit(c);
  };
  scriptSvc.setDisplayPower = [](bool on) {
    Command c(CommandType::SetDisplay);
    c.payload = on ? "{\"power\":true}" : "{\"power\":false}";
    c.source = Source::Internal;
    return engine->submit(c);
  };
  scriptSvc.sound = [](script::SoundAction a, const std::string& payload) {
    Command c = scriptSoundCommand(a, payload);
    return engine->submit(c);
  };
  scriptSvc.soundPlaying = [] { return audioRouter.isPlaying(); };
  scriptSvc.soundSinks = [] {
    const sound::Caps c = audioRouter.caps();
    return (c.buzzer ? 1 : 0) | (c.track ? 2 : 0) | (c.mp3 ? 4 : 0) | (c.radio ? 8 : 0);
  };
  scriptSvc.rotateNext = [] { engine->scriptNextApp(); };
  scriptSvc.rotatePrevious = [] { engine->scriptPreviousApp(); };
  scriptSvc.showApp = [](const std::string& id) { return engine->scriptShowApp(id); };
  scriptSvc.holdRotation = [](bool p) { engine->setScriptRotationPaused(p); };
  scriptSvc.readSource = [](const std::string& n, std::string& out) {
    return scriptStore.readSource(n, out);
  };
  scriptSvc.readStore = [](const std::string& n, std::string& out) {
    return scriptStore.readStore(n, out);
  };
  scriptSvc.monotonicMs = [] { return monotonicMs(); };
  scriptSvc.log = [](const std::string& s) { logf("%s", s.c_str()); };
  scriptSvc.logDebug = [](const std::string& s) { logdbg("%s", s.c_str()); };
  scriptSvc.freeHeap = [] { return static_cast<std::size_t>(rp2040.getFreeHeap()); };
  scriptSvc.maxAllocHeap = [] { return script::heap::picoLargestFreeBlock(); };
  {
    const script::heap::Info h = script::heap::info();
    Serial.printf("scripts: Berry heap in %s, budget %u KB\n", h.name,
                  (unsigned)(h.budgetBytes / 1024));
  }
  mqtt.setScriptingRunning(config.scriptingEnabled);
  if (config.scriptingEnabled && storageReady) {
    static script::ScriptHost host(
        apps, scriptSvc,
        [](const std::string& id) { engine->syncScriptApp(id); },
        [](const std::string& id) { engine->removeScriptApp(id); });
    scripts = &host;
    scriptMqtt.begin([](const std::string& t, const std::string& p) { mqtt.publishRaw(t, p); },
                     [](const std::string& t) { mqtt.subscribeRaw(t); },
                     [](const std::string& t) { mqtt.unsubscribeRaw(t); },
                     [](script::MqttMessage m) { scripts->pushMqttMessage(std::move(m)); });
    mqtt.setScriptBridge(&scriptMqtt);
    static script::ScriptService scriptService(
        host, [](const std::string& n, const std::string& s) { scriptStore.save(n, s); },
        [](const std::string& n) { scriptStore.remove(n); });
    engine->setScriptService(&scriptService);
    http.setScripts(
        &host,
        [](const std::string& n, std::string& out) { return scriptStore.readSource(n, out); },
        [](const std::string& n, std::string& out) { return scriptStore.readStore(n, out); });
    // Modules first, so a script that imports one finds it registered.
    for (const bool modulePass : {true, false}) {
      scriptStore.loadAll(
          [modulePass](const std::string& n, const std::string& src, const std::string& st) {
            watchdog::feed();
            if (script::parseMeta(src).module != modulePass) return;
            if (!scripts->set(n, src, st))
              Serial.printf("scripts: %s not restored (%s)\n", n.c_str(),
                            scripts->lastRefusal().c_str());
          });
    }
    if (scripts->count()) {
      scripts->staggerFirstLoops(script::kFirstLoopStaggerMs);
      Serial.printf("scripts: %u restored\n", static_cast<unsigned>(scripts->count()));
    }
  } else if (storageReady) {
    // No VM, but the sources stay listable and editable, as on the ESP32.
    static script::ScriptSourceService sourceService(
        [](const std::string& n, const std::string& s) { scriptStore.save(n, s); },
        [](const std::string& n) { scriptStore.remove(n); });
    engine->setScriptService(&sourceService);
    http.setScripts(
        nullptr,
        [](const std::string& n, std::string& out) { return scriptStore.readSource(n, out); },
        [](const std::string& n, std::string& out) { return scriptStore.readStore(n, out); },
        [] {
          std::vector<script::StoredScript> out;
          for (const std::string& n : scriptStore.names()) {
            std::string src;
            if (!scriptStore.readSource(n, src)) continue;
            out.push_back({n, script::parseMeta(src)});
          }
          return out;
        });
    Serial.println("scripts: disabled by configuration (sources stay editable)");
  }
  periphery.setButtonHook([](int btn, bool pressed) {
    return scripts && scripts->handleButtonState(engine->currentAppId(), btn, pressed);
  });
}
#endif

#if AWTRIX_FEATURE_SCRIPTING
// Rescue combo, as on the ESP32: hold A and C (left and right) for three seconds at power-on to
// turn scripting off and persist that, the way back from a script that takes the device down.
bool holdingRescueAtBoot() {
  ButtonState buttons;
  board->pollButtons(buttons);
  if (!buttons.left || !buttons.right) return false;
  const unsigned long start = millis();
  while (millis() - start < 3000) {
    board->pollButtons(buttons);
    if (!buttons.left || !buttons.right) return false;
    delay(20);
  }
  canvas->clear(0);
  text::drawText(*canvas, awtrixFont(), 0, 6, "NOSCR", 0xFF3000u);
  board->show(*canvas);
  if (config.scriptingEnabled) {
    config.scriptingEnabled = false;
    if (storageReady) config.save();
  }
  logf("boot: LEFT+RIGHT held, scripting disabled (re-enable in the web UI)");
  delay(1500);
  return true;
}
#endif

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
  // Without scripting no script service is installed: the dispatcher returns Unavailable.
  builtins.addTo(apps, effects, overlays);
  engine->setEffectRegistry(&effects);
  engine->setOverlayRegistry(&overlays);

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
    watchdog::feed(); // also the Wi-Fi join's wait callback: up to 3 x wifiConnectTimeout
    render::drawBootLogo(*canvas, awtrixFont(), bootT0, time_us_64() / 1000);
    board->show(*canvas);
  };
#if AWTRIX_FEATURE_SCRIPTING
  // Before the watchdog is armed and long before the script host exists.
  holdingRescueAtBoot();
#endif
  showBootLogo();
  const bool forceAp = holdingSelectAtBoot();
  platform::beginRadio();
  // Armed after the LittleFS mount (a first-boot format can take longer than the timeout) and the
  // radio bring-up; everything from here on either returns quickly or feeds it.
  watchdog::begin();
  network.setStatus(&engine->state().runtime().wifi);
  network.setOnJoinedFromAp([] { systemService.reboot(); });
  network.begin(config, forceAp, showBootLogo);
  static_cast<GalacticUnicornBoard*>(board)->beginAudio();
  audioRouter.setTone(board->toneSink());
  engine->state().subscribe([](StateEvent event) {
    if (event != StateEvent::SettingsChanged) return;
    const auto& s = engine->state().settings();
    audioRouter.setVolumes(s.buzzerVolume, s.dfplayerVolume, s.mp3Volume, s.radioVolume);
    audioRouter.setMuted(!s.soundEnabled);
  });
  engine->state().emit(StateEvent::SettingsChanged);
  Serial.println(api::capabilitiesJson(effects.names(), effects.paletteNames(),
                  overlays.names(), audioRouter.caps(), platform::buildFeatures()).c_str());
  timeService.apply(config.tz, config.ntpServer);
  networkWasConnected = network.isConnected();
  String mac = WiFi.macAddress();
  mac.replace(":", "");
  mac.toLowerCase();
  const uint16_t webPort = network.apMode() ? 80 :
      (config.webPort > 0 ? static_cast<uint16_t>(config.webPort) : 80);
  http.begin(webPort, *engine, *board, *canvas, mac.c_str(), config, network.apMode());
  auto capabilities = std::make_shared<const std::string>(api::capabilitiesJson(
      effects.names(), effects.paletteNames(), overlays.names(), audioRouter.caps(),
      platform::buildFeatures()));
  http.setCapabilitiesJson(capabilities);
  mqtt.setCapabilitiesJson(std::move(capabilities));
  mqttResolver = net::makeHostResolver();
  mqtt.begin(*engine, *board, config, mac.c_str(), mac.c_str(),
             config.hostname.empty() ? std::string("AWTRIX NG") : config.hostname, *mqttResolver);
  display.setScreen(canvas);
  display.setPublisher([](const std::string& topic, const std::string& payload) {
    mqtt.publish(topic, payload, false);
  });
  http.setOnConfigChanged([] {
    // The panel size is fixed, so only mirror and rotate reach the board, and they apply live.
    board->setMatrixLayout(config.matrixLayout());
    engine->state().runtime().tempDecimals = config.tempDecimals;
    logbuf::setVerbose(config.debugMode);
    timeService.apply(config.tz, config.ntpServer);
    mqtt.applyHaConfig(config);
  });
  http.setOnAssetsChanged([] {
    pipeline->invalidateIcons();
    render::clearPaletteCache();
  });
#if AWTRIX_FEATURE_SCRIPTING
  beginScripting();
#endif
  periphery.setUid(mac.c_str());
  periphery.setButtonPost(postButton);
#if AWTRIX_PICO_UDP
  if (networkWasConnected) discovery.begin(network.hostname(), config.webPort);
#endif
  static_cast<GalacticUnicornBoard*>(board)->logRefreshProgress();
}

void loop() {
  watchdog::feed();
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
  mqtt.tick();
  if (settingsDirty && storageReady && !systemService.hasPending() && nowMs - lastSettingsSaveMs > 1500) {
    awtrix::nvs::saveSettings(engine->state().settings());
    settingsDirty = false;
    lastSettingsSaveMs = nowMs;
  }
  audioRouter.tick(nowMs);
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
  engine->tick(nowMs);
#if AWTRIX_FEATURE_SCRIPTING
  if (scripts) {
    RenderCtx sctx;
    sctx.settings = &engine->state().settings();
    sctx.runtime = &engine->state().runtime();
    sctx.font = &awtrixFont(FontId::Small);
    sctx.fonts[0] = &awtrixFont(FontId::Small);
    sctx.fonts[1] = &awtrixFont(FontId::Large);
    pageClock.fill(sctx, nowMs);
    scripts->tick(sctx, engine->currentAppId(), engine->incomingAppId());
  }
  scriptStore.tick(nowMs);
#endif
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
#if AWTRIX_FEATURE_SCRIPTING
    scriptStore.flush();
#endif
    systemService.runPending();
  }
}
