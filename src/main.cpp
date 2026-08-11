#include <Arduino.h>

#include <memory>
#include <esp_heap_caps.h>
#include <LittleFS.h>
#include <WiFi.h>
#include <ctime>

#include "AppConfig.h"
#include "core/CoreEngine.h"
#include "core/FrameClock.h"
#include "core/StrCase.h"
#include "core/api/CapabilitiesJson.h"
#include "core/script/ScriptHeap.h"
#include "core/apps/AppRegistry.h"
#include "core/render/TransitionComposer.h"
#include "core/apps/SpecRenderer.h"
#include "core/effects/EffectRegistry.h"
#include "core/effects/effects/FadeEffect.h"
#include "core/effects/effects/MoreEffects.h"
#include "core/effects/effects/PlasmaEffect.h"
#include "core/effects/effects/TheaterChaseEffect.h"
#include "core/effects/overlays/RainOverlay.h"
#include "core/effects/overlays/SnowOverlay.h"
#include "core/effects/overlays/WeatherOverlays.h"
#include "core/apps/builtin/BatteryApp.h"
#include "core/apps/builtin/DateApp.h"
#include "core/apps/builtin/HumidityApp.h"
#include "core/apps/builtin/TempApp.h"
#include "core/apps/builtin/TimeApp.h"
#include "core/payload/PayloadParser.h"
#include "core/render/BootScreen.h"
#include "core/render/Canvas.h"
#include "core/render/MatrixLayout.h"
#include "core/render/ColorRamp.h"
#include "core/render/Palette.h"
#include "core/render/PowerAnimator.h"
#include "core/render/PaletteFile.h"
#include "core/render/PaletteStore.h"
#include "core/render/ProvisioningScreen.h"
#include "core/render/TextRenderer.h"
#include "core/script/ScriptHost.h"
#include "core/script/ScriptService.h"
#include "core/script/ScriptSoundCommand.h"
#include "core/script/ScriptSourceService.h"
#include "hal/BoardRegistry.h"
#include "hal/IBoard.h"
#include "media/AwtrixFontAdapter.h"
#include "core/render/RenderPipeline.h"
#include "media/DevicePageIcon.h"
#include "media/ScriptIcon.h"
#include "system/DevicePageServices.h"
#include "persistence/AppOrderStore.h"
#include "persistence/RadioStore.h"
#include "system/AudioOutEsp32.h"
#include "persistence/DeviceConfig.h"
#include "persistence/Filesystem.h"
#include "persistence/LittleFsAssetProbe.h"
#include "persistence/NvsSettings.h"
#include "persistence/ScriptStore.h"
#include "system/BootAnimator.h"
#include "system/DeviceServices.h"
#include "system/ExtMemPolicy.h"
#include "system/HeapCaps.h"
#include "system/HeapProbe.h"
#include "system/Log.h"
#include "system/MonotonicClock.h"
#include "system/PeripheryService.h"
#include "system/ScriptHttpWorker.h"
#include "transport/ScriptMqttBridge.h"
#include "transport/http/HttpApiServer.h"
#include "transport/mqtt/MqttService.h"
#include "transport/net/ArtnetService.h"
#include "transport/net/DiscoveryService.h"
#include "transport/net/NetworkService.h"

using namespace awtrix;

namespace {
IBoard* g_board = nullptr;
Canvas* g_canvas = nullptr;
sound::AudioRouter g_audio;
LittleFsAssetProbe g_assets;
DeviceDisplay* g_display = nullptr;
DeviceSystem* g_system = nullptr;
CoreEngine* g_engine = nullptr;
AppRegistry g_apps;
TimeApp g_timeApp;
DateApp g_dateApp;
TempApp g_tempApp;
HumidityApp g_humApp;
BatteryApp g_batApp;
EffectRegistry g_effects;
PlasmaEffect g_fxPlasma;
TheaterChaseEffect g_fxTheaterChase;
FadeEffect g_fxFade;
MovingLineEffect g_fxMovingLine;
BrickBreakerEffect g_fxBrick;
PingPongEffect g_fxPingPong;
RadarEffect g_fxRadar;
CheckerboardEffect g_fxCheck;
FireworksEffect g_fxFire;
PlasmaCloudEffect g_fxPlasmaCloud;
RippleEffect g_fxRipple;
SnakeEffect g_fxSnake;
PacificaEffect g_fxPacifica;
MatrixEffect g_fxMatrix;
SwirlInEffect g_fxSwirlIn;
SwirlOutEffect g_fxSwirlOut;
LookingEyesEffect g_fxEyes;
TwinklingStarsEffect g_fxStars;
ColorWavesEffect g_fxWaves;
EffectRegistry g_overlays;
RainOverlay g_ovRain;
SnowOverlay g_ovSnow;
DrizzleOverlay g_ovDrizzle;
StormOverlay g_ovStorm;
ThunderOverlay g_ovThunder;
FrostOverlay g_ovFrost;
NetworkService g_net;
HttpApiServer g_http;
std::unique_ptr<net::IHostResolver> g_hostResolver;
MqttService g_mqtt;
PeripheryService g_periphery;
BootAnimator g_bootAnim;
DiscoveryService g_disco;
ArtnetService g_artnet;
#if defined(AWTRIX_SOC_ESP32S3)
std::unique_ptr<AudioOutEsp32> g_radio;
#endif
DeviceConfig g_cfg;
bool g_settingsDirty = false;
int64_t g_lastSettingsSaveMs = -100000;
DevicePageIcon g_pageIcon;
DevicePageIcon g_pageIconB;
DevicePageClock g_pageClock;
RenderPipeline* g_pipeline = nullptr;
render::PowerAnimator* g_power = nullptr;
ScriptHttpWorker g_scriptHttp;
ScriptMqttBridge g_scriptMqtt;
ScriptIcon g_scriptIcon;
ScriptStore g_scriptStore;
script::ScriptServices g_scriptSvc;
script::ScriptHost* g_scripts = nullptr;
script::ScriptService* g_scriptService = nullptr;
bool g_netWasConnected = false;
std::string g_appliedTz, g_appliedNtp;

bool holdingSelectAtBoot() {
  ButtonState b;
  g_board->pollButtons(b);
  if (!b.select) return false;
  const unsigned long start = millis();
  while (millis() - start < 1000) {
    g_board->pollButtons(b);
    if (!b.select) return false;
    delay(20);
  }
  g_canvas->clear(0x000000u);
  text::drawText(*g_canvas, awtrixFont(), 0, 6, "SETUP", 0xFFA000u);
  g_board->show(*g_canvas);
  logf("boot: SELECT held, forcing provisioning AP (credentials kept)");
  return true;
}

// Rescue combo: hold LEFT+RIGHT for three seconds to turn scripting off and persist that. The
// way back from a script that hangs or crashes the device on every boot.
bool holdingRescueAtBoot(DeviceConfig& cfg) {
  ButtonState b;
  g_board->pollButtons(b);
  if (!b.left || !b.right) return false;
  const unsigned long start = millis();
  while (millis() - start < 3000) {
    g_board->pollButtons(b);
    if (!b.left || !b.right) return false;
    delay(20);
  }
  g_canvas->clear(0x000000u);
  text::drawText(*g_canvas, awtrixFont(), 0, 6, "NOSCR", 0xFF3000u);
  g_board->show(*g_canvas);
  if (cfg.scriptingEnabled) {
    cfg.scriptingEnabled = false;
    cfg.save();
  }
  logf("boot: LEFT+RIGHT held, scripting disabled (re-enable in the web UI)");
  delay(1500);
  return true;
}

// configTzTime restarts the SNTP client, so it only runs when the timezone or server actually
// changed. force is for the cases where the client needs restarting anyway, such as a reconnect.
void applyTimeConfig(const DeviceConfig& cfg, bool force) {
  if (!force && cfg.tz == g_appliedTz && cfg.ntpServer == g_appliedNtp) return;
  g_appliedTz = cfg.tz;
  g_appliedNtp = cfg.ntpServer;
  configTzTime(cfg.tz.c_str(), cfg.ntpServer.c_str());
  logf("ntp: syncing via %s (tz %s)", cfg.ntpServer.c_str(), cfg.tz.c_str());
}

}

void setup() {
  Serial.begin(115200);
  Serial.println();

  awtrix::noise::reseed(esp_random());

  // Filesystem first — config, palettes, icons and scripts all come off it.
  awtrix::fs::begin();

  // LittleFS is case-sensitive, but a palette named in a script or over the API rarely matches
  // the file's capitalisation, so fall back to a case-insensitive scan of the directory.
  render::setPaletteLoader([](const std::string& name, render::Palette& out) {
    if (name.find("..") != std::string::npos || name.find('/') != std::string::npos) return false;
    File f = LittleFS.open((String("/PALETTES/") + name.c_str() + ".txt").c_str(), "r");
    if (!f) {
      File dir = LittleFS.open("/PALETTES");
      for (File e = dir.openNextFile(); e; e = dir.openNextFile()) {
        std::string leaf = e.name() ? e.name() : "";
        const std::size_t slash = leaf.rfind('/');
        if (slash != std::string::npos) leaf.erase(0, slash + 1);
        if (leaf.size() <= 4 || !strcase::equalsIgnoreCase(leaf.substr(leaf.size() - 4), ".txt"))
          continue;
        if (!strcase::equalsIgnoreCase(leaf.substr(0, leaf.size() - 4), name)) continue;
        f = LittleFS.open((String("/PALETTES/") + leaf.c_str()).c_str(), "r");
        break;
      }
    }
    if (!f) return false;
    std::string text;
    text.reserve(static_cast<std::size_t>(f.size()));
    while (f.available()) text.push_back(static_cast<char>(f.read()));
    f.close();
    return render::parsePaletteFile(text, out);
  });

  // Config decides which board profile and which pins are active, so it has to be read before
  // any hardware is touched below.
  DeviceConfig& cfg = g_cfg;
  cfg.load();
  logbuf::setVerbose(cfg.debugMode);

  g_board = &activeBoard(cfg);
  g_board->begin();
  g_board->setMatrixLayout(cfg.matrixLayout());
  g_canvas = new Canvas(g_board->matrixWidth(), g_board->matrixHeight());
  g_power = new render::PowerAnimator(g_board->matrixWidth(), g_board->matrixHeight());
  g_audio.setTone(g_board->toneSink());
  g_audio.setTrack(g_board->trackSink());
  g_audio.setAssets(&g_assets);
  g_display = new DeviceDisplay();
  g_system = new DeviceSystem();
  g_system->setWakeButtonPin(cfg.pinBtnSelect);
  g_system->setDisplayOff([] {
    g_canvas->clear(0x000000u);
    g_board->show(*g_canvas);
  });
  g_engine = new CoreEngine(g_audio, *g_display, *g_system);
  g_engine->setBatteryAvailable(g_board->hasBattery());
  g_engine->setTemperatureAvailable(g_board->sensors().hasSensor());
  g_engine->setHumidityAvailable(g_board->sensors().hasHumidity());
  g_engine->setPressureAvailable(g_board->sensors().hasPressure());
  g_engine->setLightSensorAvailable(g_board->hasLightSensor());

  // Persisted user state back into the engine, then hand it the callbacks that write it out
  // again, so later reorders and station edits save themselves.
  nvs::loadSettings(g_engine->state().settings());
  apporder::load(*g_engine);
  g_engine->setOrderPersist(apporder::save);
  radiostore::load(*g_engine);
  g_engine->setStationPersist(radiostore::save);
  g_engine->state().runtime().tempDecimals = g_cfg.tempDecimals;
  // Applies a settings change to the hardware straight away but only flags the save — loop()
  // debounces it, because a slider in the web UI emits dozens of changes a second.
  g_engine->state().subscribe([](StateEvent e) {
    if (e != StateEvent::SettingsChanged) return;
    const Settings& s = g_engine->state().settings();
    g_board->applyColorGrade(render::gradeFrom(s));
    // One place for all four gains and for the mute, and it drops writes that change nothing:
    // a slider dragged across the screen must not flood a 9600 baud UART.
    g_audio.setVolumes(static_cast<uint8_t>(s.buzzerVolume),
                       static_cast<uint8_t>(s.dfplayerVolume),
                       static_cast<uint8_t>(s.mp3Volume),
                       static_cast<uint8_t>(s.radioVolume));
    g_audio.setMuted(!s.soundEnabled);
    g_settingsDirty = true;
  });
  g_engine->state().emit(StateEvent::SettingsChanged);

  // Built-ins are registered up front; the script host later adds its apps and effects to these
  // same registries, which is why they outlive setup().
  g_apps.add(&g_timeApp);
  g_apps.add(&g_dateApp);
  g_apps.add(&g_tempApp);
  g_apps.add(&g_humApp);
  g_apps.add(&g_batApp);
  g_effects.add(&g_fxPlasma);
  g_effects.add(&g_fxTheaterChase);
  g_effects.add(&g_fxFade);
  g_effects.add(&g_fxMovingLine);
  g_effects.add(&g_fxBrick);
  g_effects.add(&g_fxPingPong);
  g_effects.add(&g_fxRadar);
  g_effects.add(&g_fxCheck);
  g_effects.add(&g_fxFire);
  g_effects.add(&g_fxPlasmaCloud);
  g_effects.add(&g_fxRipple);
  g_effects.add(&g_fxSnake);
  g_effects.add(&g_fxPacifica);
  g_effects.add(&g_fxMatrix);
  g_effects.add(&g_fxSwirlIn);
  g_effects.add(&g_fxSwirlOut);
  g_effects.add(&g_fxEyes);
  g_effects.add(&g_fxStars);
  g_effects.add(&g_fxWaves);
  g_overlays.add(&g_ovRain);
  g_overlays.add(&g_ovSnow);
  g_overlays.add(&g_ovDrizzle);
  g_overlays.add(&g_ovStorm);
  g_overlays.add(&g_ovThunder);
  g_overlays.add(&g_ovFrost);

  g_engine->setOverlayRegistry(&g_overlays);
  g_engine->setEffectRegistry(&g_effects);

  RenderPipelineDeps deps;
  deps.engine = g_engine;
  deps.apps = &g_apps;
  deps.effects = &g_effects;
  deps.overlays = &g_overlays;
  deps.fonts[0] = &awtrixFont(FontId::Small);
  deps.fonts[1] = &awtrixFont(FontId::Large);
  deps.icons = &g_pageIcon;
  deps.iconsB = &g_pageIconB;
  deps.audio = &g_audio;
  deps.clock = &g_pageClock;
  g_pipeline = new RenderPipeline(g_board->matrixWidth(), g_board->matrixHeight(), deps);

  logf("boot: AWTRIX NG %s on %s", AWTRIX_NG_VERSION, g_board->name());
  logf("heap: %u KB pool, %u KB free before radio",
       (unsigned)(heap_caps_get_total_size(kGuardHeapCaps) / 1024),
       (unsigned)(heap_caps_get_free_size(kGuardHeapCaps) / 1024));
  if (const size_t psram = heap_caps_get_total_size(MALLOC_CAP_SPIRAM)) {
    logf("psram: %u KB total, %u KB free", (unsigned)(psram / 1024),
         (unsigned)(heap_caps_get_free_size(MALLOC_CAP_SPIRAM) / 1024));
    if (steerLargeAllocationsToPsram())
      logf("psram: large allocations (>%u B) steered to external RAM",
           (unsigned)kExtMemThresholdBytes);
  }
  // Checked here, well before the script host exists, so the combo still works when the problem
  // is a script that takes the device down as soon as it runs.
  holdingRescueAtBoot(cfg);
  {
    const script::heap::Info h = script::heap::info();
    logf("scripts: Berry heap in %s, budget %u KB", h.name, (unsigned)(h.budgetBytes / 1024));
    logf("scripts: %u KB IRAM free for bytecode",
         (unsigned)(heap_caps_get_free_size(MALLOC_CAP_EXEC | MALLOC_CAP_32BIT) / 1024));
  }
  const int64_t bootT0 = monotonicMs();
  auto showBootLogo = [bootT0] {
    render::drawBootLogo(*g_canvas, awtrixFont(), bootT0, monotonicMs());
    g_board->show(*g_canvas);
  };
  showBootLogo();
  const bool forceAp = holdingSelectAtBoot();
  if (!forceAp) g_bootAnim.start(*g_board, *g_canvas, awtrixFont(), bootT0);
  // Network before the services that need it. forceAp skips the join attempt entirely; joining a
  // network from the provisioning portal reboots, so the whole setup below reruns with an IP.
  g_net.setStatus(&g_engine->state().runtime().wifi);
  g_net.begin(cfg, forceAp);
  g_net.setOnJoinedFromAp([] { ESP.restart(); });
  applyTimeConfig(cfg, true);
  g_netWasConnected = g_net.isConnected();

  String mac = WiFi.macAddress();
  mac.replace(":", "");
  mac.toLowerCase();
  const std::string uid = mac.c_str();
  const uint16_t webPort =
      g_net.apMode() ? 80 : (cfg.webPort > 0 ? static_cast<uint16_t>(cfg.webPort) : 80);
  g_http.begin(webPort, *g_engine, *g_board, *g_canvas, uid, cfg, g_net.apMode());
  // Re-applies everything that can change without a restart. A different panel count would need
  // a differently sized canvas and LED buffer, so only same-width layouts are taken live.
  g_http.setOnConfigChanged([] {
    const MatrixLayout layout = g_cfg.matrixLayout();
    if (layout.width() == g_board->matrixWidth()) g_board->setMatrixLayout(layout);
    g_engine->state().runtime().tempDecimals = g_cfg.tempDecimals;
    logbuf::setVerbose(g_cfg.debugMode);
    g_mqtt.applyHaConfig(g_cfg);
    applyTimeConfig(g_cfg, false);
    if (g_net.isConnected()) {
      if (g_cfg.artnet) g_artnet.begin();
      else g_artnet.end();
    }
  });
  {
#if defined(AWTRIX_SOC_ESP32S3)
    if (AudioOutEsp32::usable(g_cfg.pinI2sBclk, g_cfg.pinI2sLrclk, g_cfg.pinI2sDout)) {
      g_radio.reset(new AudioOutEsp32(*g_engine, g_cfg.pinI2sBclk, g_cfg.pinI2sLrclk,
                                      g_cfg.pinI2sDout, g_cfg.pinI2sMclk, g_cfg.pinAmpEnable));
      g_engine->setPcmSink(g_radio.get());
      g_audio.setPcm(g_radio.get());
    }
#endif
    // Pushed once the sinks are all attached, so the PCM gains are not left at their defaults.
    g_engine->state().emit(StateEvent::SettingsChanged);
    auto caps = std::make_shared<const std::string>(api::capabilitiesJson(
        g_effects.names(), g_effects.paletteNames(), g_overlays.names(), g_audio.caps()));
    g_http.setCapabilitiesJson(caps);
    g_mqtt.setCapabilitiesJson(std::move(caps));
  }
  g_periphery.begin(*g_engine, *g_board, cfg);
  g_periphery.setUid(uid);
  g_hostResolver = net::makeHostResolver();
  g_mqtt.begin(*g_engine, *g_board, cfg, uid, uid,
               cfg.hostname.empty() ? std::string("AWTRIX NG") : cfg.hostname, *g_hostResolver);

  Publisher publisher = [](const std::string& s, const std::string& p) {
    g_mqtt.publish(s, p, false);
  };
  // The complete surface a script is allowed to reach. Everything the VM can do goes through
  // these hooks; it never sees the engine, the board or the filesystem directly.
  g_scriptSvc.http = &g_scriptHttp;
  g_scriptSvc.mqtt = &g_scriptMqtt;
  g_scriptSvc.icon = &g_scriptIcon;
  g_scriptSvc.storeSink = &g_scriptStore;
  g_scriptSvc.effects = &g_effects;
  g_scriptSvc.overlays = &g_overlays;
  g_scriptSvc.notify = [](const std::string& json) {
    DispatchDetail detail;
    return g_engine->notify(json, static_cast<uint8_t>(Source::Internal), detail) ==
           DispatchResult::Ok;
  };
  g_scriptSvc.settings = [] { return &g_engine->state().settings(); };
  g_scriptSvc.runtime = [] { return &g_engine->state().runtime(); };
  g_scriptSvc.fonts[0] = &awtrixFont(FontId::Small);
  g_scriptSvc.fonts[1] = &awtrixFont(FontId::Large);
  g_scriptSvc.panel = g_canvas;
  g_scriptSvc.setSettings = [](const std::string& json) {
    Command c(CommandType::SetSettings);
    c.payload = json;
    c.source = Source::Internal;
    return g_engine->submit(c);
  };
  g_scriptSvc.sound = [](script::SoundAction a, const std::string& payload) {
    Command c = scriptSoundCommand(a, payload);
    return g_engine->submit(c);
  };
  // One answer for "is something sounding", MP3, melody or track alike; a script asking must not
  // get a different answer than a looping notification does.
  g_scriptSvc.soundPlaying = [] { return g_audio.isPlaying(); };
  g_scriptSvc.soundSinks = [] {
    const sound::Caps c = g_audio.caps();
    return (c.buzzer ? 1 : 0) | (c.track ? 2 : 0) | (c.mp3 ? 4 : 0) |
           (c.radio ? 8 : 0);
  };
  g_scriptSvc.rotateNext = [] { g_engine->scriptNextApp(); };
  g_scriptSvc.rotatePrevious = [] { g_engine->scriptPreviousApp(); };
  g_scriptSvc.showApp = [](const std::string& id) { return g_engine->scriptShowApp(id); };
  g_scriptSvc.holdRotation = [](bool p) { g_engine->setScriptRotationPaused(p); };
  g_scriptSvc.readSource = [](const std::string& n, std::string& out) {
    return g_scriptStore.readSource(n, out);
  };
  g_scriptSvc.readStore = [](const std::string& n, std::string& out) {
    return g_scriptStore.readStore(n, out);
  };
  g_scriptSvc.monotonicMs = [] { return monotonicMs(); };
  g_scriptSvc.log = [](const std::string& s) { logf("%s", s.c_str()); };
  g_scriptIcon.setLog([](const std::string& s) { logf("[icons] %s", s.c_str()); });
  g_scriptSvc.freeHeap = [] {
    return heap_caps_get_free_size(scriptBufferHeapCaps());
  };
  g_scriptSvc.maxAllocHeap = [] {
    return heap_caps_get_largest_free_block(scriptBufferHeapCaps());
  };
  g_scriptSvc.logDebug = [](const std::string& s) { logdbg("%s", s.c_str()); };
  g_mqtt.setScriptingRunning(cfg.scriptingEnabled);
  if (cfg.scriptingEnabled) {
    static script::ScriptHost scripts(
        g_apps, g_scriptSvc,
        [](const std::string& id) { g_engine->syncScriptApp(id); },
        [](const std::string& id) { g_engine->removeScriptApp(id); });
    g_scripts = &scripts;
    g_scriptHttp.begin([](script::HttpResult r) { g_scripts->pushHttpResult(std::move(r)); });
    g_scriptMqtt.begin([](const std::string& t, const std::string& p) { g_mqtt.publishRaw(t, p); },
                       [](const std::string& t) { g_mqtt.subscribeRaw(t); },
                       [](const std::string& t) { g_mqtt.unsubscribeRaw(t); },
                       [](script::MqttMessage m) { g_scripts->pushMqttMessage(std::move(m)); });
    g_mqtt.setScriptBridge(&g_scriptMqtt);
    static script::ScriptService scriptService(
        scripts, [](const std::string& n, const std::string& s) { g_scriptStore.save(n, s); },
        [](const std::string& n) { g_scriptStore.remove(n); });
    g_scriptService = &scriptService;
    g_engine->setScriptService(&scriptService);
    g_http.setScripts(
        &scripts,
        [](const std::string& n, std::string& out) { return g_scriptStore.readSource(n, out); },
        [](const std::string& n, std::string& out) { return g_scriptStore.readStore(n, out); });
    // Two passes over the same directory: modules first, so a script that imports one finds it
    // already registered when its own turn comes.
    for (const bool modulePass : {true, false}) {
      g_scriptStore.loadAll(
          [modulePass](const std::string& n, const std::string& src, const std::string& st) {
            if (script::parseMeta(src).module != modulePass) return;
            if (!g_scripts->set(n, src, st))
              logf("scripts: %s not restored (%s)", n.c_str(), g_scripts->lastRefusal().c_str());
          });
    }
    if (g_scripts->count()) {
      g_scripts->staggerFirstLoops(script::kFirstLoopStaggerMs);
      logf("scripts: %u restored", static_cast<unsigned>(g_scripts->count()));
    }
  } else {
    // No VM, but the sources stay listable and editable so the user can fix the script that
    // forced the rescue combo and then switch scripting back on.
    static script::ScriptSourceService sourceService(
        [](const std::string& n, const std::string& s) { g_scriptStore.save(n, s); },
        [](const std::string& n) { g_scriptStore.remove(n); });
    g_engine->setScriptService(&sourceService);
    g_http.setScripts(
        nullptr,
        [](const std::string& n, std::string& out) { return g_scriptStore.readSource(n, out); },
        [](const std::string& n, std::string& out) { return g_scriptStore.readStore(n, out); },
        [] {
          std::vector<script::StoredScript> out;
          for (const std::string& n : g_scriptStore.names()) {
            std::string src;
            if (!g_scriptStore.readSource(n, src)) continue;
            out.push_back({n, script::parseMeta(src)});
          }
          return out;
        });
    logf("scripts: disabled by configuration (sources stay editable)");
  }
  g_http.setOnAssetsChanged([] {
    g_scriptIcon.invalidate();
    render::clearPaletteCache();
  });

  if (g_net.isConnected()) {
    g_disco.begin(g_net.hostname(), cfg.webPort);
    if (cfg.artnet) g_artnet.begin();
  }
  g_periphery.setButtonHook([](int btn) {
    static const char* kBtnNames[3] = {"left", "select", "right"};
    if (g_scripts && btn >= 0 && btn < 3)
      g_scripts->handleButton(g_engine->currentAppId(), kBtnNames[btn]);
    return false;
  });
  g_display->setPublisher(publisher);
  g_display->setScreen(g_canvas);

  // Everything is up; hold the intro on screen for its full length even when setup got there
  // early, then show the address long enough to be read before the first app appears.
  g_bootAnim.stop();
  while (!forceAp && monotonicMs() < bootT0 + render::kBootIntroMs) {
    showBootLogo();
    delay(10);
  }
  if (g_net.isConnected()) {
    std::string address = g_net.ip();
    if (cfg.webPort > 0 && cfg.webPort != 80) address += ":" + std::to_string(cfg.webPort);
    const int64_t t0 = monotonicMs();
    while (render::drawBootAddress(*g_canvas, awtrixFont(), address, t0, monotonicMs())) {
      g_board->show(*g_canvas);
      delay(10);
    }
  }

  Serial.print(F("AWTRIX NG "));
  Serial.print(F(AWTRIX_NG_VERSION));
  Serial.print(F(" on "));
  Serial.print(g_board->name());
  Serial.print(F(" @ "));
  Serial.println(g_net.ip().c_str());
}

namespace {
void paceFrame() {
  static int64_t nextMs = 0;
  const int64_t now = monotonicMs();
  if (nextMs <= now) {
    nextMs = now + kFramePeriodMs;
    return;
  }
  delay(static_cast<unsigned long>(nextMs - now));
  nextMs += kFramePeriodMs;
}
}

void loop() {
  probe::begin();
  const int64_t now = monotonicMs();
  {
    static uint16_t frames = 0;
    static int64_t windowStart = 0;
    ++frames;
    if (now - windowStart >= 1000) {
      g_engine->state().runtime().fps = frames;
      frames = 0;
      windowStart = now;
    }
  }
  // Collapses a burst of settings changes into one NVS write; every write costs erase budget.
  if (g_settingsDirty && now - g_lastSettingsSaveMs > 1500) {
    nvs::saveSettings(g_engine->state().settings());
    g_settingsDirty = false;
    g_lastSettingsSaveMs = now;
  }
  g_net.tick();
  const bool netConnected = g_net.isConnected();
  if (netConnected && !g_netWasConnected) applyTimeConfig(g_cfg, true);
  g_netWasConnected = netConnected;
  probe::report("net", 256);
  probe::begin();
  g_disco.tick();
  probe::report("disco", 256);
  probe::begin();
  g_http.tick();
  probe::report("http", 256);
  probe::begin();
  g_mqtt.tick();
  g_periphery.tick(now);
  g_audio.tick(now);
  probe::report("services", 256);
  probe::begin();
  g_engine->tick(now);
  probe::report("engine", 256);
  probe::begin();

  {
    RenderCtx sctx;
    sctx.settings = &g_engine->state().settings();
    sctx.runtime = &g_engine->state().runtime();
    sctx.font = &awtrixFont(FontId::Small);
    sctx.fonts[0] = &awtrixFont(FontId::Small);
    sctx.fonts[1] = &awtrixFont(FontId::Large);
    g_pageClock.fill(sctx, now);
    if (g_scripts) g_scripts->tick(sctx, g_engine->currentAppId(), g_engine->incomingAppId());
  }
  g_scriptStore.tick(now);
  probe::report("scripts+clock", 256);
  probe::begin();

  const bool wakeNotif =
      g_engine->hasNotification() && g_engine->notifications().current().wakeup;
  const bool matrixOn = !g_engine->state().runtime().matrixOff || wakeNotif;

  // What owns the screen, in order: the power on/off animation, then moodlight, the provisioning
  // screen, an Art-Net frame if one arrived, and otherwise the normal render pipeline.
  bool artnetFrame = false;
  switch (g_power->update(matrixOn, now)) {
    case render::PowerAnimator::Phase::Off:
      g_canvas->clear(0x000000u);
      break;
    case render::PowerAnimator::Phase::Out:
      g_power->composeOut(*g_canvas);
      break;
    default:
      if (g_engine->state().runtime().moodlightMode) {
        g_canvas->clear(g_engine->state().runtime().moodlightColor);
        g_board->setBrightness(g_engine->state().runtime().moodlightBrightness);
      } else if (g_net.apMode()) {
        render::drawProvisioningScreen(*g_canvas, awtrixFont(), now);
      } else if (g_artnet.tick(*g_canvas, now)) {
        artnetFrame = true;
      } else {
        g_pipeline->renderFrame(*g_canvas, now);
      }
      g_power->finish(*g_canvas);
      break;
  }

  probe::report("render", 256);
  probe::begin();
  g_board->show(*g_canvas);
  probe::report("show", 256);

  // Reboots and shutdowns wait for the power animation to play out, and any queued script store
  // is written first so nothing a script saved in its last seconds is lost.
  if (g_system->hasPending() && !g_power->busy()) {
    g_scriptStore.flush();
    g_system->runPending();
  }

  // An Art-Net sender sets its own frame rate, so back off to a short yield and let the packets
  // pace the loop instead of the frame budget.
  if (artnetFrame) {
    delay(5);
  } else {
    paceFrame();
  }
}
