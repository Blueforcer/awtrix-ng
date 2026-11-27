#include "platform/linux/remote/ScriptRunner.h"

#include <cstdio>
#include <filesystem>
#include <random>

#include "core/api/HttpProtocol.h"
#include "core/api/JsonWriter.h"
#include "core/effects/EffectNoise.h"
#include "core/render/PaletteStore.h"
#include "media/AwtrixFontAdapter.h"
#include "persistence/Filesystem.h"
#include "persistence/NvsSettings.h"
#include "platform/host/Memory.h"
#include "platform/host/Process.h"
#include "platform/host/Socket.h"
#include "platform/host/Timer.h"
#include "platform/linux/Auth.h"
#include "platform/linux/LinuxMemory.h"
#include "platform/linux/host/HostPaletteLoader.h"
#include "platform/linux/host/HostPersistence.h"
#include "platform/linux/host/HostScriptHeap.h"
#include "platform/linux/host/HostStore.h"
#include "platform/linux/host/vendor/httplib.h"
#include "platform/linux/tls/TlsTrust.h"
#include "platform_log/Output.h"
#include "system/ApplicationBootstrap.h"
#include "system/Log.h"

namespace awtrix::remote {
namespace {

std::filesystem::path privateRoot() { return std::filesystem::u8path(host::dataDir()).parent_path(); }

}

void ScriptRunner::logClock(std::time_t now, std::tm& out) {
  if (const ZoneClock* zone = zone_.load()) zone->fillLog(now, out);
  else systemLocalTime(now, out);
}

int ScriptRunner::start() {
  for (int (ScriptRunner::*phase)() : {&ScriptRunner::openData, &ScriptRunner::loadConfig,
                                       &ScriptRunner::composeEngine, &ScriptRunner::beginRole}) {
    const int status = (this->*phase)();
    if (status >= 0) return status;
  }
  composeAccess();
  composeCapabilities();
  composeScriptServices();
  startScripts();
  return startServices();
}

int ScriptRunner::openData() {
  std::error_code webuiError;
  if (!std::filesystem::is_regular_file(std::filesystem::u8path(o_.webui), webuiError) || webuiError) {
    std::fprintf(stderr, "Cannot read web UI: %s\n", o_.webui.c_str());
    return 2;
  }
  os::keepNewFilesPrivate();
  if (!os::startSockets()) {
    std::fputs("Cannot use the network\n", stderr);
    return 1;
  }
  if (!o_.caFile.empty()) tls::setPublicAnchorsFile(o_.caFile);
  host::setDataDir(o_.data);
  if (!fs::begin()) {
    std::fputs("Cannot create data directory\n", stderr);
    return 1;
  }
  if (!lock_.acquire(host::hostPath("/.lock"))) {
    std::fputs("Data directory is already in use or not writable\n", stderr);
    return 1;
  }
  zone_.store(&clock_);
  g_logClock = logClock;
  os::stopOnRequest(stopping_);
  noise::reseed(std::random_device{}());
  return -1;
}

int ScriptRunner::loadConfig() {
  cfg_.webPort = 8080;
  cfg_.load();
  cfg_.panelConfigurable = false;
  cfg_.gpioConfigurable = false;
  logbuf::setVerbose(cfg_.debugMode);
  applyConfig();
  std::string uid;
  if (!host::readFile(host::hostPath("/identity"), uid) || uid.size() != 16) {
    std::random_device random;
    char text[17];
    std::snprintf(text, sizeof(text), "%08x%08x", random(), random());
    uid = text;
    if (!host::writeFile(host::hostPath("/identity"), uid)) return 1;
  }
  uid_ = uid;
  render::setPaletteLoader(host::loadPalette);
  return -1;
}

int ScriptRunner::composeEngine() {
  const int width = o_.width, height = o_.height;
  target_.apps = apps_.registry();
  board_ = std::make_unique<LinuxBoard>(width, height);
  pictureLoader_.emplace([](const std::string& line) { logf("%s", line.c_str()); });
  remoteImages_.emplace(*pictureLoader_, [] { return monotonicMs(); });
  remoteImages_->start();
  iconA_.emplace(&*remoteImages_);
  iconB_.emplace(&*remoteImages_);
  pageZoom_.emplace(width, height, *iconA_);
  audio_.setAssets(&assets_);
  services_.emplace(ApplicationServices{audio_, display_, system_, clock_, awtrixFontCatalog(), &*iconA_, &*iconB_,
                                        [this](uint8_t value) { board_->setBrightness(value); }});
  services_->pageZoom = &*pageZoom_;
  app_.emplace(width, height, *services_, target_.apps);
  if (!app_->ready()) {
    std::fputs("Display frame allocation failed\n", stderr);
    return 1;
  }
  app_->markSensors(*board_);
  engine().state().runtime().tempDecimals = cfg_.tempDecimals;
  deviceFacts_.emplace(engine(), *board_, uid_, target_.id, "unknown");
  bootstrap::restoreUserState(engine());
  app_->bindOutputs(*board_, dirty_);
  app_->applyOutputs(*board_);
  board_->begin();
  if (!board_->displayReady()) {
    std::fprintf(stderr, "Display startup failed: %s\n", board_->name());
    return 1;
  }
  return -1;
}

int ScriptRunner::beginRole() {
  std::string error;
  if (!role_.begin({o_.clock, o_.keyFile, o_.clockUrl}, static_cast<uint16_t>(o_.port), o_.width, o_.height,
                   error)) {
    std::fprintf(stderr, "%s\n", error.c_str());
    return 2;
  }
  role_.setOnTimezone([this](const std::string& tz, const std::string& tzName) {
    if (tz.empty() || (tz == cfg_.tz && tzName == cfg_.tzName)) return;
    cfg_.tz = tz;
    cfg_.tzName = tzName;
    applyConfig();
  });
  role_.setOnBroker([this](const RemoteRole::Broker& broker) {
    scriptOnlyMqtt_.reset();
    if (!broker.enabled || broker.host.empty() || !scripts_ || !resolver_) return;
    DeviceConfig config;
    config.mqttEnabled = true;
    config.mqttHost = broker.host;
    config.mqttPort = static_cast<uint16_t>(broker.port);
    config.mqttUser = broker.user;
    config.mqttPass = broker.pass;
    const std::string prefix = (broker.prefix.empty() ? o_.clock : broker.prefix) + "/scripthost";
    scriptOnlyMqtt_ = std::make_unique<ScriptOnlyMqtt>();
    scriptOnlyMqtt_->begin(config, o_.clock + "-sh", prefix, *resolver_, *scripts_, scriptMqtt_);
    logf("script host: MQTT for scripts via %s:%ld", broker.host.c_str(), broker.port);
  });
  logf("script host: runs the scripts of clock %s at %s", o_.clock.c_str(), o_.clockUrl.c_str());
  return -1;
}

void ScriptRunner::composeAccess() {
  httpOptions_.listenAddress = "0.0.0.0";
  httpOptions_.allowCrossOrigin = true;
  httpOptions_.keyLogin = true;
  httpOptions_.loginGuard = [key = role_.keyHex()](const httplib::Request& request, httplib::Response& response) {
    return request.method == "OPTIONS" ||
           (request.get_header_value_count("Authorization") == 1 &&
            api::hostKeyOk(request.get_header_value("Authorization"), key)) ||
           auth::deny(response, api::unauthorized());
  };
}

void ScriptRunner::composeCapabilities() {
  capabilities_.emplace(false, cfg_.scriptingEnabled, false);
  capabilities_->hostOfClock();
  resolver_ = net::makeHostResolver();
  display_.setScreen(&app_->canvas());
  httpOptions_.deviceState = [this](bool scripting) { return deviceFacts_->json(scripting); };
  platform_.emplace(PlatformDescriptor{target_.id, {o_.width, o_.height, cfg_.panelConfigurable},
                                       cfg_.gpioConfigurable});
  PlatformDescriptor& platform = *platform_;
  platform.lightSensor = board_->hasLightSensor();
  platform.display.limits = target_.displayLimits;
  platform.display.ready = board_->displayReady() && app_->ready();
  engine().setClockFaces(target_.clockFaces);
  LinuxCapabilities& capabilities = *capabilities_;
  capabilities.clockFaces = target_.clockFaces;
  capabilities.enlargeApps = pageZoom_->available();
  http_.setCapabilitiesJson(
      app_->capabilitiesJson(platform, [&](api::JsonWriter& writer) { capabilities.write(writer); }));
  const DeviceCapabilities own = DeviceCapabilities::from(audio_.caps(), &platform, capabilities.needs());
  role_.setOnCapabilities([this, own](const std::string& clock) {
    http_.setDeviceCapabilities(clockCapabilities(clock, own));
  });
  http_.setOnConfigChanged([this] {
    applyConfig();
    engine().state().runtime().tempDecimals = cfg_.tempDecimals;
  });
}

void ScriptRunner::composeScriptServices() {
  scriptIcon_.setPanelSize(o_.width, o_.height);
  scriptIcon_.setRemoteImages(&*remoteImages_);
  role_.bindIcons(scriptIcon_);
  scriptServices_.instructionLimit = target_.scriptInstructionLimit;
  oauthVault_.emplace((privateRoot() / "script-private").string());
  oauthService_.emplace(
      *oauthVault_, [this](const oauth::HttpCall& call) { return oauthTransport_(call); },
      host::ScriptFiles::savedSource, [] { return monotonicMs(); });
  oauthService_->setInterrupt([this] { oauthTransport_.interrupt(); });
  oauthScripting_.emplace(*oauthService_);
  oauthApi_.emplace(*oauthService_, SavedScripts::savedNames, httpOptions_.keyLogin);
  secrets_.emplace((privateRoot() / "script-secrets").string(), host::ScriptFiles::savedSource);
  secretScripting_.emplace(*secrets_);
  secretApi_.emplace(*secrets_, SavedScripts::savedNames, httpOptions_.keyLogin);
  scriptStores_.add(*oauthService_);
  scriptStores_.add(*secrets_);
  httpOptions_.platformHandler = [this, scripting = cfg_.scriptingEnabled](const httplib::Request& request,
                                                                          httplib::Response& response) {
    return scripting && (oauthApi_->handle(request, response) || secretApi_->handle(request, response));
  };
  app_->configureScripts(scriptServices_);
  application_.emplace(engine(), role_);
  scriptServices_.application = &*application_;
  scriptServices_.audioStats = [this](int64_t now, bool onScreen, awtrix::audio::FrameStats& out) {
    return analysis_.analysis(static_cast<audio::AnalysisSource>(engine().state().settings().musicSource), now, out,
                              onScreen);
  };
  scriptServices_.http = &scriptHttp_;
  scriptServices_.mqtt = &scriptMqtt_;
  if (cfg_.scriptingEnabled) {
    extensions_.push_back(&tcp_);
    extensions_.push_back(&*oauthScripting_);
    extensions_.push_back(&*secretScripting_);
    scriptStores_.dropStale();
    oauthService_->begin();
  }
}

void ScriptRunner::startScripts() {
  bootstrap::configureScriptFiles(scriptServices_, store_, scriptIcon_);
  std::uint64_t availableAtStart = 0;
  os::availableMemory(availableAtStart);
  const std::uint64_t bodyReserve = httpOptions_.maxBodyBytes;
  script::heap::configureHost("system", scriptHeapBudget(availableAtStart), [bodyReserve] {
    std::uint64_t available = 0;
    if (!os::availableMemory(available) || available <= bodyReserve) return std::size_t{0};
    return static_cast<std::size_t>(available - bodyReserve);
  });
  scriptServices_.freeHeap = [] {
    std::uint64_t available = 0;
    return os::availableMemory(available) ? static_cast<std::size_t>(available) : std::size_t{0};
  };
  if (cfg_.scriptingEnabled)
    store_.onSourceWritten([this](const std::string& name) { scriptStores_.sourceSaved(name); });
  scriptSlots_.begin(*app_, scriptServices_, cfg_.scriptingEnabled,
      [this](const std::string& name, const std::string& source) { store_.save(name, source); },
      [this, enabled = cfg_.scriptingEnabled](const std::string& name) {
        store_.remove(name);
        if (enabled) scriptStores_.sourceRemoved(name);
      });
  scripts_ = scriptSlots_.host();
  if (!scripts_) return;
  role_.bindScripts(*scripts_, engine());
  scriptExtensions_ = std::make_unique<script::ExtensionHost>(*scripts_, std::move(extensions_));
  bootstrap::bindScriptHttp(*scripts_, scriptHttp_);
  bootstrap::restoreScripts(*scripts_, store_, 0);
}

int ScriptRunner::startServices() {
  buttons_.begin(engine(), cfg_, &app_->launcher());
  bootstrap::bindScriptButtons(buttons_, engine(), scripts_);
  http_.setScripts(scripts_, scriptServices_.readSource, scriptServices_.readStore,
                   [this] { return store_.storedScripts(); });
  http_.setOnAssetsChanged([this] {
    scriptIcon_.invalidate();
    app_->invalidateIcons();
    render::clearPaletteCache();
  });
  http_.setReleaseFiles([this](const std::string& path) { audio_.release(path); });
  if (!http_.begin(static_cast<uint16_t>(o_.port), engine(), app_->canvas(), uid_, cfg_, o_.webui, httpOptions_)) {
    scriptHttp_.stop();
    return 1;
  }
  std::printf("AWTRIX NG %s %s @ http://%s:%d (%dx%d)\n", AWTRIX_NG_VERSION, board_->name(),
              httpOptions_.listenAddress.c_str(), o_.port, o_.width, o_.height);
  std::fflush(stdout);
  return -1;
}

void ScriptRunner::run() {
  pacer_.start(monotonicMs());
  while (!stopping_ && runFrame(pacer_.tickMs())) os::sleepUntil(pacer_.advance(monotonicMs()) * 1000);
}

bool ScriptRunner::runFrame(int64_t now) {
  app_->beginFrame(now);
  deviceFacts_->tick(now, true);
  http_.tick();
  app_->tick(now, nullptr);
  role_.tick(now, *app_, *services_, scripts_);
  if (o_.idleSeconds > 0 && role_.silentMs(now) >= int64_t{o_.idleSeconds} * 1000) {
    logf("script host: clock %s silent for %d s, stopping", o_.clock.c_str(), o_.idleSeconds);
    silent_ = true;
    return false;
  }
  if (scriptOnlyMqtt_) scriptOnlyMqtt_->tick(now, true);
  store_.tick(now);
  board_->show(app_->canvas());
  if (stopping_) return false;
  saveDue(now);
  return true;
}

void ScriptRunner::saveDue(int64_t now) {
  if (!settingsSaver_.due(now)) return;
  if (dirty_) nvs::saveSettings(engine().state().settings());
  host::persistence::flushPending();
  dirty_ = host::persistence::pending(host::persistence::Document::Settings);
  settingsSaver_.saved(now);
}

bool ScriptRunner::persist() {
  store_.flush();
  if (dirty_) nvs::saveSettings(engine().state().settings());
  const bool flushed = host::persistence::flushPending() && !store_.hasPending();
  dirty_ = host::persistence::pending(host::persistence::Document::Settings);
  return flushed;
}

int ScriptRunner::stop() {
  const bool resetAll = system_.resetAll;
  if (!resetAll) persist();
  http_.stop();
  scriptHttp_.stop();
  remoteImages_->stop();
  bool saved = true;
  if (!resetAll) {
    saved = persist();
    if (!saved) std::fputs("Shutdown could not persist all application state\n", stderr);
  }
  std::error_code error;
  if (system_.resetUserSettings && !resetAll)
    std::filesystem::remove(std::filesystem::u8path(host::hostPath("/settings.json")), error);
  if (resetAll && !eraseAll(error)) return 1;
  return !error && saved ? 0 : 1;
}

bool ScriptRunner::eraseAll(std::error_code& error) {
  if (!scriptStores_.eraseAll()) {
    std::fputs("Factory reset could not erase private script data\n", stderr);
    return false;
  }
  const auto root = std::filesystem::u8path(host::dataDir());
  std::filesystem::directory_iterator entry(root, error), end;
  while (!error && entry != end) {
    if (entry->path().filename() != ".lock") std::filesystem::remove_all(entry->path(), error);
    if (!error) entry.increment(error);
  }
  return true;
}

void ScriptRunner::applyConfig() {
  clock_.setRule(cfg_.tz);
  logbuf::setVerbose(cfg_.debugMode);
}

net::FillRequest ScriptRunner::fillSecrets() {
  return [this](const std::string& owner, std::string& url, net::RequestHeaders& headers, net::RequestBody& body) {
    return secrets_->fill(owner, url, headers, body);
  };
}

}
