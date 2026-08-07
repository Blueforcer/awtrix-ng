#if defined(_WIN32)
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <winsock2.h>
#include <ws2tcpip.h>
#endif

#include <Arduino.h>

#if defined(_WIN32)
#include <windows.h>
#include <timeapi.h>
#endif

#include <cstdio>
#include <cstring>
#include <filesystem>
#include <functional>
#include <memory>
#include <random>
#include <string>

#include "AppConfig.h"
#include "core/CoreEngine.h"
#include "core/FrameClock.h"
#include "core/SocProfileJson.h"
#include "core/StrCase.h"
#include "core/api/StateJson.h"
#include "core/Transitions.h"
#include "core/apps/AppRegistry.h"
#include "core/apps/SpecRenderer.h"
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
#include "core/net/WifiLink.h"
#include "core/payload/PayloadParser.h"
#include "core/render/Canvas.h"
#include "core/render/MatrixLayout.h"
#include "core/render/ColorRamp.h"
#include "core/render/Palette.h"
#include "core/render/PaletteFile.h"
#include "core/render/PaletteStore.h"
#include "core/render/PowerAnimator.h"
#include "core/render/RenderPipeline.h"
#include "core/script/ScriptHost.h"
#include "core/script/ScriptService.h"
#include "core/script/ScriptSourceService.h"
#include "media/AwtrixFontAdapter.h"
#include "media/DevicePageIcon.h"
#include "media/ScriptIcon.h"
#include "persistence/AppOrderStore.h"
#include "persistence/RadioStore.h"
#include "sim/FakeClipService.h"
#include "sim/FakeRadioService.h"
#include "persistence/DeviceConfig.h"
#include "persistence/Filesystem.h"
#include "persistence/NvsSettings.h"
#include "sim/SimBoard.h"
#include "sim/SimHttpServer.h"
#include "sim/SimPageServices.h"
#include "sim/SimPeriphery.h"
#include "sim/SimScriptServices.h"
#include "sim/SimStore.h"
#include "sim/SimTerminalMatrix.h"
#include "system/Log.h"
#include "system/MonotonicClock.h"
#include "transport/ScriptMqttBridge.h"
#include "transport/mqtt/MqttService.h"

using namespace awtrix;
namespace stdfs = std::filesystem;

namespace {

class SimSoundService : public ISoundService {
 public:
  explicit SimSoundService(IBoard& board) : board_(board) {}
  // Mirrors DeviceSound: a stored MP3 clip wins over a melody of the same name.
  bool playSound(const std::string& payload) override {
    if (clips_) {
      const std::string path = sound::clipPathFor(payload);
      if (!path.empty() && stdfs::exists(stdfs::u8path(sim::hostPath(path))))
        return clips_->playClip(path);
    }
    return board_.sound().playFile(payload);
  }
  void playRtttl(const std::string& rtttl) override { board_.sound().playRtttl(rtttl); }
  void r2d2(const std::string&) override {
    board_.sound().playRtttl("r2d2:d=4,o=5,b=240:16c6,16g6,16e6,16a6,16g6,16e7");
  }
  void stop() override {
    if (clips_) clips_->stopClip();
    board_.sound().stop();
  }

  void setClips(IClipService* clips) { clips_ = clips; }

 private:
  IBoard& board_;
  IClipService* clips_ = nullptr;
};

class SimDisplayService : public IDisplayService {
 public:
  void configure(std::function<void(const std::string&, const std::string&)> pub,
                 const Canvas* screen) {
    pub_ = std::move(pub);
    screen_ = screen;
  }
  void sendScreen() override {
    if (pub_ && screen_) pub_("state/screen", buildScreenJson(*screen_));
  }

 private:
  std::function<void(const std::string&, const std::string&)> pub_;
  const Canvas* screen_ = nullptr;
};

class SimSystemService : public ISystemService {
 public:
  void reboot() override { logf("sim: reboot request ignored (restart the binary instead)"); }
  void sleep(uint64_t durationMs) override {
    logf("sim: sleep(%llu ms) ignored", static_cast<unsigned long long>(durationMs));
  }
  void factoryReset() override { logf("sim: factory reset ignored (delete simdata/ instead)"); }
  void resetSettings() override { logf("sim: settings reset ignored (delete simdata/settings.json)"); }
};

SimBoard g_board;
Canvas* g_canvas = nullptr;
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
MovingLineEffect g_fxMovingLine; BrickBreakerEffect g_fxBrick; PingPongEffect g_fxPingPong;
RadarEffect g_fxRadar; CheckerboardEffect g_fxCheck; FireworksEffect g_fxFire;
PlasmaCloudEffect g_fxPlasmaCloud; RippleEffect g_fxRipple; SnakeEffect g_fxSnake;
PacificaEffect g_fxPacifica; MatrixEffect g_fxMatrix; SwirlInEffect g_fxSwirlIn;
SwirlOutEffect g_fxSwirlOut; LookingEyesEffect g_fxEyes; TwinklingStarsEffect g_fxStars;
ColorWavesEffect g_fxWaves;
EffectRegistry g_overlays;
RainOverlay g_ovRain;
SnowOverlay g_ovSnow;
DrizzleOverlay g_ovDrizzle;
StormOverlay g_ovStorm;
ThunderOverlay g_ovThunder;
FrostOverlay g_ovFrost;
SimHttpServer g_http;
std::unique_ptr<net::IHostResolver> g_hostResolver;
MqttService g_mqtt;
ScriptMqttBridge g_scriptMqtt;
SimTerminalMatrix g_term;
SimPeriphery g_periphery;
std::unique_ptr<sim::FakeRadioService> g_radio;
std::unique_ptr<sim::FakeClipService> g_clips;
DeviceConfig g_cfg;
bool g_settingsDirty = false;
int64_t g_lastSettingsSaveMs = -100000;
DevicePageIcon g_pageIcon;
DevicePageIcon g_pageIconB;
SimPageSound* g_pageSound = nullptr;
SimPageClock g_pageClock;
RenderPipeline* g_pipeline = nullptr;
render::PowerAnimator* g_power = nullptr;
sim::SimScriptHttp g_scriptHttp;
ScriptIcon g_scriptIcon;
sim::SimScriptStore g_scriptStore;
script::ScriptServices g_scriptSvc;
script::ScriptHost* g_scripts = nullptr;

// Without this the host would spin as fast as it can and peg a core, and animations would run at a
// speed nobody will ever see on the device.
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

int main(int argc, char** argv) {
// Windows sleeps in ~15 ms steps by default, which would make paceFrame overshoot every frame.
#if defined(_WIN32)
  timeBeginPeriod(1);
#endif
  awtrix::noise::reseed(std::random_device{}());
  uint16_t port = 8080;
  std::string webuiFile = "webui/index.html";
  SimTerminalMatrix::Mode termMode = SimTerminalMatrix::Mode::Auto;
  for (int i = 1; i < argc; ++i) {
    const bool hasVal = i + 1 < argc;
    if (std::strcmp(argv[i], "--port") == 0 && hasVal)
      port = static_cast<uint16_t>(atoi(argv[++i]));
    else if (std::strcmp(argv[i], "--data") == 0 && hasVal)
      sim::setDataDir(argv[++i]);
    else if (std::strcmp(argv[i], "--webui") == 0 && hasVal)
      webuiFile = argv[++i];
    else if (std::strcmp(argv[i], "--matrix") == 0)
      termMode = SimTerminalMatrix::Mode::On;
    else if (std::strcmp(argv[i], "--no-matrix") == 0)
      termMode = SimTerminalMatrix::Mode::Off;
  }

  awtrix::fs::begin();

  // Exact filename first, then a case-insensitive sweep of /PALETTES, because the device's flash
  // filesystem is not case sensitive and a name typed either way has to resolve the same.
  render::setPaletteLoader([](const std::string& name, render::Palette& out) {
    if (name.find("..") != std::string::npos || name.find('/') != std::string::npos) return false;
    std::string text;
    if (!sim::readFile(sim::hostPath("/PALETTES/" + name + ".txt"), text)) {
      bool found = false;
      std::error_code ec;
      for (stdfs::directory_iterator it(stdfs::u8path(sim::hostPath("/PALETTES")), ec), end;
           !ec && it != end && !found; it.increment(ec)) {
        const std::string leaf = it->path().filename().u8string();
        if (leaf.size() <= 4 || !strcase::equalsIgnoreCase(leaf.substr(leaf.size() - 4), ".txt"))
          continue;
        if (!strcase::equalsIgnoreCase(leaf.substr(0, leaf.size() - 4), name)) continue;
        found = sim::readFile(sim::hostPath("/PALETTES/" + leaf), text);
      }
      if (!found) return false;
    }
    return render::parsePaletteFile(text, out);
  });

  DeviceConfig& cfg = g_cfg;
  cfg.load();
  logbuf::setVerbose(cfg.debugMode);

  g_board.begin();
  g_board.setMatrixLayout(cfg.matrixLayout());
  g_canvas = new Canvas(g_board.matrixWidth(), g_board.matrixHeight());
  g_power = new render::PowerAnimator(g_board.matrixWidth(), g_board.matrixHeight());
  g_pageSound = new SimPageSound(g_board);
  static SimSoundService sound(g_board);
  static SimDisplayService display;
  static SimSystemService system;
  g_engine = new CoreEngine(sound, display, system);
  g_engine->setBatteryAvailable(g_board.hasBattery());
  g_engine->setTemperatureAvailable(g_board.sensors().hasSensor());
  g_engine->setHumidityAvailable(g_board.sensors().hasHumidity());
  g_engine->setPressureAvailable(g_board.sensors().hasPressure());
  g_engine->setLightSensorAvailable(g_board.hasLightSensor());

  // The host has no NetworkService: the compat WiFi reports permanently associated, so say so
  // once instead of leaving the link status at its "never configured" default.
  net::applyWifiAssoc(g_engine->state().runtime().wifi, net::WifiAssoc::Connected, true, "sim",
                      "127.0.0.1");

  nvs::loadSettings(g_engine->state().settings());
  apporder::load(*g_engine);
  g_engine->setOrderPersist(apporder::save);
  radiostore::load(*g_engine);
  g_engine->setStationPersist(radiostore::save);
  g_engine->state().subscribe([](StateEvent e) {
    if (e != StateEvent::SettingsChanged) return;
    const Settings& s = g_engine->state().settings();
    g_board.applyColorGrade(render::gradeFrom(s));
    g_board.sound().setVolume(static_cast<uint8_t>(s.volume));
    g_settingsDirty = true;
  });
  // Kick the subscriber once so the settings just loaded from disk reach the board and the sound
  // backend, instead of only taking effect after the first edit.
  g_engine->state().emit(StateEvent::SettingsChanged);


  g_apps.add(&g_timeApp);
  g_apps.add(&g_dateApp);
  g_apps.add(&g_tempApp);
  g_apps.add(&g_humApp);
  g_apps.add(&g_batApp);
  g_effects.add(&g_fxPlasma);
  g_effects.add(&g_fxTheaterChase);
  g_effects.add(&g_fxFade);
  g_effects.add(&g_fxMovingLine); g_effects.add(&g_fxBrick); g_effects.add(&g_fxPingPong);
  g_effects.add(&g_fxRadar); g_effects.add(&g_fxCheck); g_effects.add(&g_fxFire);
  g_effects.add(&g_fxPlasmaCloud); g_effects.add(&g_fxRipple); g_effects.add(&g_fxSnake);
  g_effects.add(&g_fxPacifica); g_effects.add(&g_fxMatrix); g_effects.add(&g_fxSwirlIn);
  g_effects.add(&g_fxSwirlOut); g_effects.add(&g_fxEyes); g_effects.add(&g_fxStars);
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
  deps.sound = g_pageSound;
  deps.clock = &g_pageClock;
  g_pipeline = new RenderPipeline(g_board.matrixWidth(), g_board.matrixHeight(), deps);

  if (g_term.begin(termMode, port, g_engine))
    g_board.onShow = [](const Canvas& c, uint8_t bri) { g_term.render(c, bri); };

  logf("boot: AWTRIX NG %s on %s (data: %s)", AWTRIX_NG_VERSION, g_board.name(),
       sim::dataDir().c_str());

  // The device derives this from its chip ID; a fixed one keeps MQTT topics and the hostname stable
  // across restarts, which is what the end-to-end tests rely on.
  const std::string uid = "simulator";
  if (!g_http.begin(port, *g_engine, g_board, *g_canvas, uid, cfg, webuiFile)) return 1;
  g_http.setOnConfigChanged([] {
    // A layout change only takes effect live while the width still matches: a different width would
    // mean rebuilding the canvas, the power animator and the pipeline, so that waits for a restart.
    const MatrixLayout layout = g_cfg.matrixLayout();
    if (layout.width() == g_board.matrixWidth()) g_board.setMatrixLayout(layout);
    if (g_scripts)
      g_scripts->setLimit(g_cfg.scriptLimit < 0 ? 0 : static_cast<std::size_t>(g_cfg.scriptLimit));
    script::setMaxSourceBytes(static_cast<std::size_t>(g_cfg.scriptMaxBytes));
    logbuf::setVerbose(g_cfg.debugMode);
  });
  {
    auto jsonList = [](const std::vector<std::string>& names) {
      std::string out = "[";
      bool first = true;
      for (const auto& n : names) {
        if (!first) out += ',';
        out += '"' + n + '"';
        first = false;
      }
      out += ']';
      return out;
    };
  g_radio = std::unique_ptr<sim::FakeRadioService>(new sim::FakeRadioService(*g_engine));
  g_engine->setRadioService(g_radio.get());
  g_clips = std::unique_ptr<sim::FakeClipService>(new sim::FakeClipService(*g_engine));
  sound.setClips(g_clips.get());
  g_pageSound->setClips(g_clips.get());

    std::string caps = "{\"effects\":" + jsonList(g_effects.names()) +
                       ",\"paletteEffects\":" + jsonList(g_effects.paletteNames()) +
                       ",\"transitions\":" + transitionsJson() +
                       ",\"overlays\":" + jsonList(g_overlays.names()) +
                       ",\"palettes\":[\"Cloud\",\"Lava\",\"Ocean\",\"Forest\",\"Stripe\","
                       "\"Party\",\"Heat\",\"Rainbow\"]"
                       ",\"radio\":" +
                       std::string(g_engine->radioAvailable() ? "true" : "false") +
                       ",\"audio\":" +
                       std::string(g_engine->radioAvailable() ? "true" : "false") +
                       ",\"gpio\":" + pins::toJson(pins::activeProfile()) + "}";
    g_http.setCapabilitiesJson(caps);
    g_mqtt.setCapabilitiesJson(std::make_shared<const std::string>(caps));
  }
  g_periphery.begin(*g_engine, g_board, cfg);
  g_hostResolver = net::makeHostResolver();
  g_mqtt.begin(*g_engine, g_board, cfg, uid, uid,
               cfg.hostname.empty() ? std::string("AWTRIX NG") : cfg.hostname, *g_hostResolver);
  display.configure([](const std::string& s, const std::string& p) { g_mqtt.publish(s, p, false); },
                    g_canvas);
  g_periphery.setButtonHook([](int btn) {
    static const char* kBtnNames[3] = {"left", "select", "right"};
    if (g_scripts && btn >= 0 && btn < 3)
      g_scripts->handleButton(g_engine->currentAppId(), kBtnNames[btn]);
    return false;
  });

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
    Command c(a == script::SoundAction::Play    ? CommandType::PlaySound
              : a == script::SoundAction::Rtttl ? CommandType::PlayRtttl
                                                : CommandType::StopSound);
    c.payload = payload;
    c.source = Source::Internal;
    return g_engine->submit(c);
  };
  g_scriptSvc.soundPlaying = [] {
    return (g_clips && g_clips->clipPlaying()) || g_board.sound().isPlaying();
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
  g_scriptSvc.logDebug = [](const std::string& s) { logdbg("%s", s.c_str()); };
  if (cfg.scriptingEnabled) {
    static script::ScriptHost scripts(
        g_apps, g_scriptSvc,
        [](const std::string& id) { g_engine->syncScriptApp(id); },
        [](const std::string& id) { g_engine->removeScriptApp(id); });
    g_scripts = &scripts;
    scripts.setLimit(cfg.scriptLimit < 0 ? 0 : static_cast<std::size_t>(cfg.scriptLimit));
  script::setMaxSourceBytes(static_cast<std::size_t>(cfg.scriptMaxBytes));
    g_scriptHttp.begin([](script::HttpResult r) { g_scripts->pushHttpResult(std::move(r)); });
    g_scriptMqtt.begin([](const std::string& t, const std::string& p) { g_mqtt.publishRaw(t, p); },
                       [](const std::string& t) { g_mqtt.subscribeRaw(t); },
                       [](const std::string& t) { g_mqtt.unsubscribeRaw(t); },
                       [](script::MqttMessage m) { g_scripts->pushMqttMessage(std::move(m)); });
    g_mqtt.setScriptBridge(&g_scriptMqtt);
    static script::ScriptService scriptService(
        scripts, [](const std::string& n, const std::string& s) { g_scriptStore.save(n, s); },
        [](const std::string& n) { g_scriptStore.remove(n); });
    g_engine->setScriptService(&scriptService);
    g_http.setScripts(
        &scripts,
        [](const std::string& n, std::string& out) { return g_scriptStore.readSource(n, out); },
        [](const std::string& n, std::string& out) { return g_scriptStore.readStore(n, out); });
    // Two passes: library modules have to exist before the scripts that import them are compiled.
    for (const bool modulePass : {true, false}) {
      g_scriptStore.loadAll(
          [modulePass](const std::string& n, const std::string& src, const std::string& st) {
            if (script::parseMeta(src).module != modulePass) return;
            if (!g_scripts->set(n, src, st))
              logf("scripts: %s not restored (limit %d reached)", n.c_str(), g_cfg.scriptLimit);
          });
    }
    if (g_scripts->count()) logf("scripts: %u restored", static_cast<unsigned>(g_scripts->count()));
  } else {
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

  if (!g_term.active()) {
    std::printf("AWTRIX NG %s simulator @ http://localhost:%u  (Ctrl+C to quit)\n",
                AWTRIX_NG_VERSION, static_cast<unsigned>(port));
    std::fflush(stdout);
  }

  for (;;) {
    const int64_t now = monotonicMs();
    g_board.setNow(now);
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
    // Settings writes are debounced: the UI can change a value every frame, the file is rewritten
    // at most every 1.5 s. On the device this is what spares the NVS wear budget.
    if (g_settingsDirty && now - g_lastSettingsSaveMs > 1500) {
      nvs::saveSettings(g_engine->state().settings());
      g_settingsDirty = false;
      g_lastSettingsSaveMs = now;
    }
    g_http.tick();
    g_mqtt.tick();
    g_periphery.tick(now);
    g_board.sound().tick();
    g_engine->tick(now);
    if (g_radio) g_radio->tick(now);
    if (g_clips) g_clips->tick(now);

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

    // A notification marked wakeup lights the panel even when the user switched the matrix off.
    const bool wakeNotif =
        g_engine->hasNotification() && g_engine->notifications().current().wakeup;
    const bool matrixOn = !g_engine->state().runtime().matrixOff || wakeNotif;

    // Panel power is animated rather than switched: Off blanks, Out plays the shutdown wipe, and
    // otherwise the frame is rendered normally and finish() overlays the wake-up animation.
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
          g_board.setBrightness(g_engine->state().runtime().moodlightBrightness);
        } else {
          g_pipeline->renderFrame(*g_canvas, now);
        }
        g_power->finish(*g_canvas);
        break;
    }
    g_board.show(*g_canvas);
    paceFrame();
  }
}
