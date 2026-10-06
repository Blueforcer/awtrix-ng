#include <Arduino.h>

#include <memory>
#include <new>
#include <esp_heap_caps.h>
#include <LittleFS.h>
#include <WiFi.h>
#include <ctime>

#include "AppConfig.h"
#include "core/ApplicationRuntime.h"
#include "core/ScreenDisplay.h"
#include "system/ApplicationBootstrap.h"
#include "core/apps/DefaultApps.h"
#include "core/FrameClock.h"
#include "core/net/DeviceUrl.h"
#include "core/StrCase.h"
#include "core/api/CapabilitiesJson.h"
#include "core/script/ScriptHeap.h"
#include "core/apps/AppRegistry.h"
#include "core/render/TransitionComposer.h"
#include "core/apps/SpecRenderer.h"
#include "core/effects/EffectRegistry.h"
#include "core/effects/EffectNoise.h"
#include "core/payload/PayloadParser.h"
#include "core/render/BootScreen.h"
#include "core/render/Canvas.h"
#include "core/render/MatrixLayout.h"
#include "core/render/ColorRamp.h"
#include "core/render/Palette.h"
#include "core/render/PaletteFile.h"
#include "core/render/PaletteStore.h"
#include "core/render/ProvisioningScreen.h"
#include "core/render/TextRenderer.h"
#include "core/script/ScriptHost.h"
#include "core/script/ScriptService.h"
#include "core/script/ScriptSourceService.h"
#include "hal/BoardRegistry.h"
#include "hal/IBoard.h"
#include "media/AwtrixFontAdapter.h"
#include "core/render/RenderPipeline.h"
#include "media/DevicePageIcon.h"
#include "media/ScriptIcon.h"
#include "core/SystemPageClock.h"
#include "persistence/AppOrderStore.h"
#include "persistence/RadioStore.h"
#include "system/AudioOutEsp32.h"
#include "persistence/DeviceConfig.h"
#include "persistence/Filesystem.h"
#include "persistence/AssetProbe.h"
#include "persistence/NvsSettings.h"
#include "persistence/ScriptStore.h"
#include "system/BootAnimator.h"
#include "system/DeviceServices.h"
#include "system/ExtMemPolicy.h"
#include "system/HeapCaps.h"
#include "system/HeapProbe.h"
#include "system/DisplayProbe.h"
#include "system/Log.h"
#include "system/MonotonicClock.h"
#include "system/PeripheryService.h"
#include "system/ScriptHttpWorker.h"
#include "transport/DeviceStateJson.h"
#include "transport/ScriptMqttBridge.h"
#include "transport/http/HttpApiServer.h"
#include "transport/mqtt/MqttService.h"
#include "transport/net/ArtnetService.h"
#include "transport/net/DiscoveryService.h"
#include "transport/net/MirrorLink.h"
#include "transport/net/NetworkService.h"

using namespace awtrix;

namespace {
IBoard* g_board = nullptr;
Canvas* g_canvas = nullptr;
sound::AudioRouter g_audio;
AssetProbe g_assets;
ScreenDisplay* g_display = nullptr;
DeviceSystem* g_system = nullptr;
CoreEngine* g_engine = nullptr;
NetworkService g_net;
HttpApiServer g_http;
std::unique_ptr<net::IHostResolver> g_hostResolver;
MqttService g_mqtt;
PeripheryService g_periphery;
BootAnimator g_bootAnim;
DiscoveryService g_disco;
ArtnetService g_artnet;
MirrorLink g_mirror;
#if defined(AWTRIX_SOC_ESP32S3)
std::unique_ptr<AudioOutEsp32> g_radio;
#endif
DeviceConfig g_cfg;
bool g_settingsDirty = false;
SettingsSaver g_settingsSaver{-100000};
DevicePageIcon g_pageIcon;
DevicePageIcon g_pageIconB;
SystemPageClock g_pageClock;
ScriptHttpWorker g_scriptHttp;
ScriptMqttBridge g_scriptMqtt;
ScriptIcon g_scriptIcon;
ScriptStore<> g_scriptStore;
script::ScriptServices g_scriptSvc;
script::ScriptHost* g_scripts = nullptr;
script::ScriptSlots g_scriptSlots;
bool g_netWasConnected = false;
std::string g_appliedTz, g_appliedNtp;
std::unique_ptr<DefaultApps> g_builtinApps;
std::unique_ptr<ApplicationRuntime> g_runtime;
bool g_usablePsram = false;

void* allocateFrame(std::size_t bytes) {
  if (g_usablePsram) {
    if (void* memory = heap_caps_malloc(bytes, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT)) return memory;
    if (bytes > 32u * 8u * sizeof(uint32_t)) return nullptr;
  }
  constexpr std::size_t kReserve = 64u * 1024u;
  if (heap_caps_get_free_size(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT) < bytes + kReserve)
    return nullptr;
  return heap_caps_malloc(bytes, MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
}

void refreshCapabilities() {
  PlatformDescriptor platform{"esp32", {g_board->matrixWidth(), g_board->matrixHeight(), true}};
#if defined(AWTRIX_SOC_ESP32S3)
  platform.id = "esp32s3";
#endif
  platform.lightSensor = g_board->hasLightSensor();
  platform.display.limits = kEspDisplayLimits;
  const int requestedWidth = espMatrixWidth(g_cfg.panelWidth, g_cfg.panels);
  platform.display.requestedWidth = requestedWidth > 0 ? requestedWidth : kMatrixWidthMin;
  platform.display.estimatedWireTimeUs = static_cast<uint32_t>(platform.display.pixelCount() * 30u);
  platform.display.ready = g_board->displayReady() && g_runtime->ready();
  auto caps = std::make_shared<const std::string>(g_runtime->capabilitiesJson(platform));
  g_http.setCapabilitiesJson(caps);
  g_http.setDeviceCapabilities(DeviceCapabilities::from(g_audio.caps(), &platform));
  g_mqtt.setCapabilitiesJson(std::move(caps));
}

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

  render::setPaletteLoader([](const std::string& name, render::Palette& out) {
    return render::loadPaletteFile(name, out, [](const std::string& leaf, std::string& text) {
      File f = LittleFS.open((String("/PALETTES/") + leaf.c_str()).c_str(), "r");
      if (!f) return false;
      text.reserve(static_cast<std::size_t>(f.size()));
      while (f.available()) text.push_back(static_cast<char>(f.read()));
      return true;
    }, [](const render::PaletteNameVisitor& visit) {
      File dir = LittleFS.open("/PALETTES");
      for (File e = dir.openNextFile(); e; e = dir.openNextFile()) {
        std::string_view leaf = e.name() ? e.name() : "";
        const std::size_t slash = leaf.rfind('/');
        if (slash != std::string_view::npos) leaf.remove_prefix(slash + 1);
        if (visit(leaf)) break;
      }
    });
  });

  // Config decides which board profile and which pins are active, so it has to be read before
  // any hardware is touched below.
  DeviceConfig& cfg = g_cfg;
  cfg.load();
  logbuf::setVerbose(cfg.debugMode);
  // Frame memory policy; plain malloc stays internal until the RMT buffers exist.
  g_usablePsram = psramSteerable();
  // Plain malloc stays out of PSRAM until steerLargeAllocationsToPsram().
  if (g_usablePsram) heap_caps_malloc_extmem_enable(static_cast<std::size_t>(-1));
  render::setFrameAllocator({allocateFrame, heap_caps_free});

  g_board = &activeBoard(cfg);
  g_audio.setTone(g_board->toneSink());
  g_audio.setTrack(g_board->trackSink());
  g_audio.setAssets(&g_assets);
  g_display = new ScreenDisplay();
  g_system = new DeviceSystem();
  g_system->setWakeButtonPin(cfg.pinBtnSelect);
  g_system->setDisplayOff([] {
    g_canvas->clear(0x000000u);
    g_board->show(*g_canvas);
  });
  ApplicationServices services{g_audio, *g_display, *g_system, g_pageClock,
      awtrixFontCatalog(), &g_pageIcon, &g_pageIconB,
      [](uint8_t brightness) { g_board->setBrightness(brightness); }};
  services.lightConfig = [] { return g_cfg.lightConfig(); };
  bootstrap::mirrorServices(services, g_mirror);
  g_builtinApps.reset(new DefaultApps);
  g_runtime.reset(new ApplicationRuntime(g_board->matrixWidth(), g_board->matrixHeight(), services,
                                          g_builtinApps->registry()));
  if (!g_runtime->ready()) {
    logf("matrix: no frame memory, using 32x8");
    g_runtime.reset();
    g_board->setMatrixLayout(MatrixLayout{});
    g_runtime.reset(new ApplicationRuntime(32, 8, services, g_builtinApps->registry()));
  }
  g_board->begin();
  if (!g_board->displayReady() &&
      (g_board->matrixWidth() != 32 || g_board->matrixHeight() != 8)) {
    logf("matrix: no driver memory, using 32x8");
    g_runtime.reset();
    g_board->setMatrixLayout(MatrixLayout{});
    g_runtime.reset(new ApplicationRuntime(32, 8, services, g_builtinApps->registry()));
    g_runtime->ready();
    g_board->begin();
  }
  if (!g_runtime->ready()) logf("matrix: no frame memory");
  g_scriptIcon.setPanelSize(g_board->matrixWidth(), g_board->matrixHeight());
  g_canvas = &g_runtime->canvas();
  g_engine = &g_runtime->engine();
  g_mirror.begin(g_canvas->width(), g_canvas->height(), g_engine->state().runtime().mirror);
  g_mirror.configure(cfg);
  g_runtime->markSensors(*g_board);
  bootstrap::restoreUserState(*g_engine);
  g_engine->state().runtime().tempDecimals = g_cfg.tempDecimals;
  g_runtime->bindOutputs(*g_board, g_settingsDirty);
  g_engine->state().emit(StateEvent::SettingsChanged);

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
  g_net.begin(cfg, g_engine->state().runtime().wifi, forceAp);
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
  g_http.setOnError([](const std::string& event) { g_mqtt.publishError(event); });
  // Mapping changes can apply live. Either dimension changing requires new permanent buffers.
  g_http.setOnConfigChanged([] {
    const MatrixLayout layout = g_cfg.matrixLayout();
    if (layout.width() == g_board->matrixWidth() && layout.height() == g_board->matrixHeight())
      g_board->setMatrixLayout(layout);
    refreshCapabilities();
    g_engine->state().runtime().tempDecimals = g_cfg.tempDecimals;
    logbuf::setVerbose(g_cfg.debugMode);
    g_mqtt.applyHaConfig(g_cfg);
    applyTimeConfig(g_cfg, false);
    if (g_net.isConnected()) {
      if (g_cfg.artnet) g_artnet.begin();
      else g_artnet.end();
    }
    g_mirror.configure(g_cfg);
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
    // Before the MQTT service starts: its Home Assistant discovery is built from these.
    refreshCapabilities();
  }
  g_periphery.begin(*g_engine, *g_board, cfg, &g_runtime->launcher());
  g_periphery.setUid(uid);
  g_hostResolver = net::makeHostResolver();
  bootstrap::beginMqtt(g_mqtt, *g_engine, *g_board, cfg, uid, *g_hostResolver);
  g_mqtt.setDeviceState([uid](bool scripting) {
    return buildDeviceStateJson(*g_engine, *g_board, uid, scripting);
  });
  g_mqtt.setWebUrl([webPort] { return net::deviceUrl(g_net.ip(), webPort); });

  g_runtime->configureScripts(g_scriptSvc);
  g_scriptSvc.http = &g_scriptHttp;
  g_scriptSvc.mqtt = &g_scriptMqtt;
  bootstrap::configureScriptFiles(g_scriptSvc, g_scriptStore, g_scriptIcon);
#if defined(AWTRIX_SOC_ESP32S3)
  if (g_radio)
    g_scriptSvc.audioStats = [](int64_t now, audio::FrameStats& out) {
      return g_radio->analysis(now, out);
    };
#endif
  g_scriptIcon.setLog([](const std::string& s) { logf("[icons] %s", s.c_str()); });
  g_scriptSvc.freeHeap = [] {
    return heap_caps_get_free_size(scriptBufferHeapCaps());
  };
  g_scriptSvc.maxAllocHeap = [] {
    return heap_caps_get_largest_free_block(scriptBufferHeapCaps());
  };
  g_mqtt.setScriptingRunning(cfg.scriptingEnabled);
  g_scriptSlots.begin(*g_runtime, g_scriptSvc, cfg.scriptingEnabled,
      [](const std::string& name, const std::string& source) { g_scriptStore.save(name, source); },
      [](const std::string& name) { g_scriptStore.remove(name); });
  g_scripts = g_scriptSlots.host();
  if (g_scripts) {
    bootstrap::bindScriptHttp(*g_scripts, g_scriptHttp);
    bootstrap::bindScriptMqtt(*g_scripts, g_mqtt, g_scriptMqtt);
    bootstrap::restoreScripts(*g_scripts, g_scriptStore, script::kFirstLoopStaggerMs);
  } else {
    logf("scripts: disabled by configuration (sources stay editable)");
  }
  g_http.setScripts(g_scripts, g_scriptSvc.readSource, g_scriptSvc.readStore,
                    [] { return g_scriptStore.storedScripts(); });
  g_http.setOnAssetsChanged([] {
    bootstrap::assetsChanged(*g_runtime, g_scriptIcon);
  });
  // Stops playback from a file before it is deleted or replaced.
  g_http.setReleaseFiles([](const std::string& path) { g_audio.release(path); });

  if (g_net.isConnected()) {
    g_disco.begin(g_net.hostname(), cfg.webPort);
    if (cfg.artnet) g_artnet.begin();
  }
  bootstrap::bindScriptButtons(g_periphery, *g_engine, g_scripts);
  bootstrap::bindDisplay(*g_display, *g_canvas, g_mqtt);

  // Everything is up; hold the intro on screen for its full length even when setup got there
  // early, then show the version and address long enough to be read before the first app appears.
  g_bootAnim.stop();
  if (!forceAp) {
    while (monotonicMs() < bootT0 + render::kBootIntroMs) {
      showBootLogo();
      delay(10);
    }
    render::BootInfo info{AWTRIX_NG_VERSION, ""};
    if (g_net.isConnected()) {
      info.address = net::deviceAddress(g_net.ip(), webPort);
    }
    const int64_t t0 = monotonicMs();
    while (render::drawBootInfo(*g_canvas, awtrixFont(), info, t0, monotonicMs())) {
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
  static int64_t lastConfigRetryMs = 0;
  if (g_cfg.persistencePending && monotonicMs() - lastConfigRetryMs >= 5000) {
    lastConfigRetryMs = monotonicMs();
    if (g_cfg.save()) logf("config: saved");
  }
  const uint32_t servicesStart = displayprobe::start();
  probe::begin();
  const int64_t now = monotonicMs();
  g_runtime->beginFrame(now);
  // Collapses a burst of settings changes into one NVS write; every write costs erase budget.
  if (g_settingsDirty && g_settingsSaver.due(now)) {
    nvs::saveSettings(g_engine->state().settings());
    g_settingsDirty = false;
    g_settingsSaver.saved(now);
  }
  g_net.tick(static_cast<uint32_t>(now));
  const bool netConnected = g_net.isConnected();
  if (netConnected && !g_netWasConnected) applyTimeConfig(g_cfg, true);
  g_netWasConnected = netConnected;
  probe::report("net", 256);
  probe::begin();
  g_disco.tick();
  g_mirror.tick(now, netConnected && !g_net.apMode());
  probe::report("disco", 256);
  probe::begin();
  g_http.tick();
  probe::report("http", 256);
  probe::begin();
  g_mqtt.tick();
  g_periphery.tick(now);
  displayprobe::record(displayprobe::Phase::Services, servicesStart);
  const uint32_t tickStart = displayprobe::start();
  g_runtime->tick(now, g_scripts, [](ApplicationTickStage stage) {
    if (stage == ApplicationTickStage::Audio) {
      probe::report("services", 256);
      probe::begin();
    } else if (stage == ApplicationTickStage::Engine) {
      probe::report("engine", 256);
      probe::begin();
    }
  });
  g_scriptStore.tick(now);
  displayprobe::record(displayprobe::Phase::Tick, tickStart);
  probe::report("scripts+clock", 256);
  probe::begin();

  const uint32_t renderStart = displayprobe::start();
  const bool artnetFrame = g_runtime->render(now, [](Canvas& canvas, int64_t frameMs, void*) {
    if (g_net.apMode()) {
      render::drawProvisioningScreen(canvas, awtrixFont(), frameMs);
      return PlatformFrame::Rendered;
    }
    return g_artnet.tick(canvas, frameMs) ? PlatformFrame::ExternalRate : PlatformFrame::None;
  });

  probe::report("render", 256);
  displayprobe::record(displayprobe::Phase::Render, renderStart);
  displayprobe::rendered();
  probe::begin();
  g_board->show(*g_canvas);
  probe::report("show", 256);
  displayprobe::report(g_canvas->width(), g_canvas->height());

  // Reboots and shutdowns wait for the power animation to play out, and any queued script store
  // is written first so nothing a script saved in its last seconds is lost.
  if (g_system->hasPending() && !g_runtime->powerBusy()) {
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
