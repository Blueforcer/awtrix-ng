#include "platform/linux/LinuxRuntime.h"

#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <random>
#include <fcntl.h>
#include <sys/file.h>
#include <sys/stat.h>
#include <unistd.h>

#include "AppConfig.h"
#include "core/FrameClock.h"
#include "core/api/CapabilitiesJson.h"
#include "core/effects/EffectNoise.h"
#include "core/net/DeviceUrl.h"
#include "core/render/PaletteStore.h"
#include "media/AwtrixFontAdapter.h"
#include "persistence/Filesystem.h"
#include "platform/linux/host/HostPaletteLoader.h"
#include "platform/linux/host/HostPersistence.h"
#include "platform/linux/host/HostScriptHeap.h"
#include "platform/linux/host/HostStore.h"
#include "platform/linux/LinuxAdminSecurity.h"
#include "platform/linux/LinuxMemory.h"
#include "platform/linux/tls/TlsTrust.h"
#include "platform/tc002/contract/RuntimeContract.h"
#include "platform/tc002/runtime/BootIntroWide.h"
#include "platform/tc002/runtime/MusicPitch.h"
#include "platform/tc002/speech/SpeechModelVoice.h"
#include "platform/tc002/voice/VoiceMqtt.h"
#include "system/ApplicationBootstrap.h"
#include "system/Log.h"

namespace awtrix {
namespace {

// A script's saved source, "" when there is none. Reads the file, which any thread may do.
std::string savedSource(const std::string& name) {
  std::string source;
  if (!ScriptStore<host::ScriptFiles>::readSavedSource(name, source))
    source.clear();
  return source;
}

class SpeakerBootSound final : public BootSoundClock {
 public:
  explicit SpeakerBootSound(Tc002Speaker& speaker) : speaker_(speaker) {}
  Start poll(int64_t& audibleAtMs) override {
    switch (speaker_.bootSoundStart(audibleAtMs)) {
      case tc002::Tc002AudioSink::SystemStart::Audible: return Start::Audible;
      case tc002::Tc002AudioSink::SystemStart::Pending: return Start::Pending;
      default: return Start::Failed;
    }
  }
  void cancel() override { speaker_.stopBootSound(); }

 private:
  Tc002Speaker& speaker_;
};

}

int LinuxRuntime::start() {
  for (int (LinuxRuntime::*phase)() : {&LinuxRuntime::selectTarget, &LinuxRuntime::openChannels,
                                        &LinuxRuntime::loadConfig, &LinuxRuntime::composeEngine}) {
    const int status = (this->*phase)();
    if (status >= 0) return status;
  }
  composeAccess();
  composeConnectivity();
  composeScriptServices();
  composeBluetooth();
  composeExtensions();
  composePlatformRoutes();
  startScripts();
  return startServices();
}

int LinuxRuntime::selectTarget() {
  if (o_.boardType == "headless") {
    headlessApps_ = std::make_unique<DefaultApps>();
    headlessTarget_.apps = headlessApps_->registry();
  }
  target_ = o_.boardType == "tc002" ? &tc002Target_.policy() : &headlessTarget_;
  const bool physicalDisplay = target_->physicalDisplay;
  if (o_.physicalInput && !physicalDisplay) {
    std::fputs("TC002 input requires --board tc002\n", stderr); return 2;
  }
  if (o_.speakerRequested && !physicalDisplay) {
    std::fputs("TC002 audio requires --board tc002\n", stderr); return 2;
  }
  if (physicalDisplay &&
      (o_.width != target_->displayLimits.minWidth || o_.height != target_->displayLimits.minHeight)) {
    std::fputs("TC002 requires a 52x16 display\n", stderr); return 2;
  }
  return -1;
}

int LinuxRuntime::openChannels() {
  std::string securityError;
  if (!configureLinuxAdminSecurity(o_.administration, o_.data, static_cast<uint16_t>(o_.port), httpOptions_,
                                   securityError)) {
    std::fprintf(stderr, "Administrative security: %s\n", securityError.c_str()); return 2;
  }
  std::error_code webuiError;
  if (!std::filesystem::is_regular_file(std::filesystem::u8path(o_.webui), webuiError) || webuiError) {
    std::fprintf(stderr, "Cannot read web UI: %s\n", o_.webui.c_str()); return 2;
  }
  ::umask(0077);
  std::string trustError;
  if (!o_.caFile.empty() && !loadTlsTrust(o_.caFile, trustError))
    logf("tls: %s; HTTPS connections will fail", trustError.c_str());
  if (o_.webUpdate()) {
    if (o_.updateOptions.releaseRoot.empty())
      o_.updateOptions.releaseRoot =
          std::filesystem::absolute(std::filesystem::u8path(o_.webui)).parent_path().parent_path().u8string();
    update_ = std::make_unique<Tc002Update>(o_.updateOptions);
  }
  std::string linkError;
  if (o_.supervised && !link_.open(SupervisorLink::kDescriptor, linkError)) {
    std::fprintf(stderr, "Supervisor channel refused: %s\n", linkError.c_str()); return 2;
  }
  if (o_.speakerRequested && !validateSupervisorSocket(Tc002Speaker::kDescriptor, linkError)) {
    std::fprintf(stderr, "Speaker channel refused: %s\n", linkError.c_str()); return 2;
  }
  if (o_.supervised && !link_.send(tc002::encodeHello(AWTRIX_NG_VERSION))) {
    std::fputs("Supervisor channel closed before hello\n", stderr); return 1;
  }
  performance_.emplace(kFramePeriodMs * 1000, target_->physicalDisplay, [] { return monotonicUs(); });
  std::string performanceError;
  if (!o_.performancePath.empty() && !performance_->open(o_.performancePath, performanceError)) {
    std::fprintf(stderr, "Performance report: %s\n", performanceError.c_str()); return 2;
  }
  host::setDataDir(o_.data);
  if (target_->uploadReserveBytes) host::setUploadReserve(target_->uploadReserveBytes);
  if (target_->maxBodyBytes) httpOptions_.maxBodyBytes = target_->maxBodyBytes;
  if (!fs::begin()) { std::fputs("Cannot create data directory\n", stderr); return 1; }
  lock_ = ::open(host::hostPath("/.lock").c_str(), O_RDWR | O_CREAT | O_CLOEXEC, 0600);
  if (lock_ < 0 || ::flock(lock_, LOCK_EX | LOCK_NB) != 0) {
    std::fputs("Data directory is already in use or not writable\n", stderr); return 1;
  }
  std::signal(SIGINT, onSignal);
  std::signal(SIGTERM, onSignal);
  std::signal(SIGPIPE, SIG_IGN);
  noise::reseed(std::random_device{}());
  return -1;
}

int LinuxRuntime::loadConfig() {
  const LinuxAdminConfig& administration = o_.administration;
  cfg_.webPort = administration.lan ? o_.port : 8080;
  cfg_.load();
  // The display size comes from the command line and nothing here drives GPIO.
  cfg_.panelConfigurable = false;
  cfg_.gpioConfigurable = false;
  mqttAllowed_ = !administration.hardened || !administration.mqttCaFile.empty();
  if (administration.hardened && mqttAllowed_) {
    std::string ca;
    if (!readLinuxProvisionedFile(administration.mqttCaFile, o_.data, false, ca)) {
      std::fputs("MQTT security: CA must be a protected regular file outside --data\n", stderr); return 2;
    }
    auto anchors = tls::TrustAnchors::fromPem(ca);
    if (anchors)
      mqttTls_ = std::make_unique<LinuxMqttTls>(std::make_shared<tls::PeerTrust>(std::move(anchors), false));
    if (!mqttTls_ || !mqttTls_->valid() || (cfg_.mqttEnabled && (cfg_.mqttUser.empty() || cfg_.mqttPass.empty()))) {
      std::fputs("MQTT security: valid CA and nonempty broker username/password required\n", stderr); return 2;
    }
  }
  if (administration.lan) {
    brokerTrust_ = std::make_unique<tls::BrokerTrust>(cfg_.mqttTlsPin);
    brokerTrustApi_ = std::make_unique<tls::BrokerTrustApi>(*brokerTrust_);
    if (cfg_.mqttTls) mqttTls_ = std::make_unique<LinuxMqttTls>(brokerTrust_->peer());
  }
  logbuf::setVerbose(cfg_.debugMode);
  applyConfig();
  std::string& uid = o_.uid;
  if (uid.empty() && (o_.supervised || !host::readFile(host::hostPath("/identity"), uid) || uid.size() != 16)) {
    std::random_device random;
    char text[17];
    std::snprintf(text, sizeof(text), "%08x%08x", random(), random());
    uid = text;
    if (!o_.supervised && !host::writeFile(host::hostPath("/identity"), uid)) return 1;
  }
  render::setPaletteLoader(host::loadPalette);
  return -1;
}

int LinuxRuntime::composeEngine() {
  const int width = o_.width, height = o_.height;
  if (target_->physicalDisplay) {
    auto adapter = std::make_unique<Tc002Board>();
    tc002_ = adapter.get();
    selectedBoard_ = std::move(adapter);
  } else {
    selectedBoard_ = std::make_unique<LinuxBoard>(width, height);
  }
  if (o_.physicalInput && !input_.begin()) {
    std::fprintf(stderr, "Input startup failed: %s\n", input_.error().c_str()); return 1;
  }
  pictureLoader_.emplace([](const std::string& line) { logf("%s", line.c_str()); });
  remoteImages_.emplace(*pictureLoader_, [] { return monotonicMs(); });
  remoteImages_->start();
  iconA_.emplace(&*remoteImages_);
  iconB_.emplace(&*remoteImages_);
  pageZoom_.emplace(width, height, *iconA_);
  audio_.setAssets(&assets_);
  services_.emplace(ApplicationServices{audio_, display_, system_, clock_, awtrixFontCatalog(), &*iconA_, &*iconB_,
                                        [this](uint8_t value) { board().setBrightness(value); }});
  services_->pageZoom = &*pageZoom_;
  layoutLimits_.preparedBytes = 256u * 1024u;
  layoutBudget_ = std::make_shared<layout::Budget>(layoutLimits_);
  bootstrap::mirrorServices(*services_, mirror_);
  app_.emplace(width, height, *services_, target_->apps);
  if (!app_->ready()) { std::fputs("Display frame allocation failed\n", stderr); return 1; }
  mirror_.begin(width, height, engine().state().runtime().mirror);
  mirror_.configure(cfg_);
  layoutResources_ = {&awtrixFontCatalog(), &*iconA_, &app_->effects(), &app_->overlays()};
  layoutPayload_.emplace(DisplayProfile{width, height, false}, layoutResources_, layoutBudget_);
  payload::setKeyHandlers(layoutPayload_->handlers());
  std::shared_ptr<speech::SpeechVoice> voiceModel;
  if (!o_.speechVoice.empty()) {
    std::string voiceError;
    if (auto model = speech::SpeechModel::load(o_.speechVoice, voiceError))
      voiceModel = std::make_shared<speech::ModelVoice>(std::move(model));
    else
      logf("speech: %s: %s; no voice", o_.speechVoice.c_str(), voiceError.c_str());
  }
  if (o_.speakerRequested)
    speaker_ = std::make_unique<Tc002Speaker>(engine(), audio_, Tc002Speaker::kDescriptor, std::move(voiceModel));
  app_->markSensors(board());
  engine().state().runtime().tempDecimals = cfg_.tempDecimals;
  supervision_.emplace(link_, engine(), board(), cfg_, clock_);
  quickSettings_.emplace(engine().state());
  if (o_.supervised) supervision_->start();
  deviceFacts_.emplace(engine(), board(), o_.uid, target_->id, o_.startReason.empty() ? "unknown" : o_.startReason);
  if (o_.supervised) deviceFacts_->addSource(*supervision_);
  if (update_) deviceFacts_->addSource(*update_);
  if (target_->defaultVolume >= 0) engine().state().settings().volume = target_->defaultVolume;
  bootstrap::restoreUserState(engine());
  app_->bindOutputs(board(), dirty_);
  app_->applyOutputs(board());
  board().begin();
  if (!board().displayReady()) {
    std::fprintf(stderr, "Display startup failed: %s\n", tc002_ ? tc002_->error().c_str() : board().name());
    return 1;
  }
  return -1;
}

void LinuxRuntime::composeAccess() {
  const bool lan = o_.administration.lan;
  provisioning_.emplace(o_.port);
  provisioning_->setActive(o_.supervised && lan);
  lanLogin_.update(cfg_);
  if (lan)
    httpOptions_.loginGuard = [this](const httplib::Request& request, httplib::Response& response) {
      if (provisioning_->active() && !provisioning_->admit(request, response)) return false;
      return lanLogin_.admit(request, response);
    };
  if (o_.supervised && lan) {
    httpOptions_.routeGuard = [this](const httplib::Request& request, httplib::Response& response) {
      if (provisioning_->active() && !provisioning_->admit(request, response)) return false;
      return lanLogin_.admit(request, response);
    };
  }
  if (o_.supervised) {
    httpOptions_.configAccepted = [this](DeviceConfig& next) { supervision_->configAccepted(next); };
    httpOptions_.wifiScan = [this](std::string& body) { return supervision_->wifiScan(monotonicMs(), body); };
  }
  if (update_) {
    httpOptions_.updateUpload = [this](const httplib::Request& request, httplib::Response& response,
                                       const httplib::ContentReader& content) {
      update_->upload(request, response, content);
    };
    httpOptions_.updateUploadBytes = Tc002Update::kMaxRequestBytes;
  }
  listenAddress_ = httpOptions_.listenAddress;
}

void LinuxRuntime::composeConnectivity() {
  const bool supervised = o_.supervised;
  const int port = o_.port;
  platformCapabilities_.emplace(o_.bluetooth, cfg_.scriptingEnabled, o_.height > 8 ? &layoutLimits_ : nullptr);
  resolver_ = net::makeHostResolver();
  if (mqttAllowed_) {
    bootstrap::beginMqtt(mqtt_, engine(), board(), cfg_, o_.uid, *resolver_, mqttTls_.get());
    mqtt_.setDeviceState([this](bool scripting) { return deviceFacts_->json(scripting); });
    mqtt_.setWebUrl([this, port] { return net::deviceUrl(deviceFacts_->ipAddress(), port); });
    bootstrap::bindDisplay(display_, app_->canvas(), mqtt_);
  }
  httpOptions_.deviceState = [this](bool scripting) { return deviceFacts_->json(scripting); };
  platform_.emplace(PlatformDescriptor{target_->id, {o_.width, o_.height, cfg_.panelConfigurable},
                                       cfg_.gpioConfigurable});
  PlatformDescriptor& platform = *platform_;
  platform.lightSensor = board().hasLightSensor();
  platform.display.limits = target_->displayLimits;
  platform.display.ready = board().displayReady() && app_->ready();
  engine().setClockFaces(target_->clockFaces);
  platform.microphonePcm = supervised;
  LinuxCapabilities& capabilities = *platformCapabilities_;
  capabilities.clockFaces = target_->clockFaces;
  capabilities.voice = supervised && speaker_;
  capabilities.mqttTls = o_.administration.lan;
  capabilities.bootSound = supervised && speaker_;
  capabilities.enlargeApps = pageZoom_->available();
  std::string capabilitiesText =
      app_->capabilitiesJson(platform, [&](api::JsonWriter& writer) { capabilities.write(writer); });
  auto shared = std::make_shared<const std::string>(std::move(capabilitiesText));
  http_.setCapabilitiesJson(*shared);
  http_.setDeviceCapabilities(DeviceCapabilities::from(audio_.caps(), &platform, capabilities.needs()));
  mqtt_.setCapabilitiesJson(shared);
  http_.setOnConfigChanged([this] {
    applyConfig();
    engine().state().runtime().tempDecimals = cfg_.tempDecimals;
    lanLogin_.update(cfg_);
    if (brokerTrust_) brokerTrust_->setPin(cfg_.mqttTlsPin);
    if (mqttAllowed_) mqtt_.applyHaConfig(cfg_);
    mirror_.configure(cfg_);
  });
  supervision_->setOnBatteryAppeared([this] { if (mqttAllowed_) mqtt_.applyHaConfig(cfg_); });
  if (mqttAllowed_) http_.setOnError([this](const std::string& event) { mqtt_.publishError(event); });
}

void LinuxRuntime::composeScriptServices() {
  const bool supervised = o_.supervised;
  scriptIcon_.setPanelSize(o_.width, o_.height);
  scriptIcon_.setRemoteImages(&*remoteImages_);
  scriptServices_.instructionLimit = target_->scriptInstructionLimit;
  microphone_.emplace([this, supervised](std::string request) { return supervised && link_.send(std::move(request)); });
  if (supervised && speaker_) {
    const auto privateDirectory = std::filesystem::u8path(host::dataDir()).parent_path() / "voice-private";
    voice_ = std::make_unique<tc002::voice::VoiceRuntime>(privateDirectory.string(), *speaker_, *microphone_,
      [this](std::string request) { return link_.send(std::move(request)); });
  }
  oauthVault_.emplace((std::filesystem::u8path(host::dataDir()).parent_path() / "script-private").string());
  oauthService_.emplace(
      *oauthVault_, [this](const oauth::HttpCall& call) { return oauthTransport_(call); }, savedSource,
      [] { return monotonicMs(); });
  oauthService_->setInterrupt([this] { oauthTransport_.interrupt(); });
  oauthScripting_.emplace(*oauthService_);
  oauthApi_.emplace(*oauthService_, [] { return ScriptStore<host::ScriptFiles>::savedNames(); });
  // Script sign-in exists as the script host does: decided once, at start.
  httpOptions_.platformHandler = [this, scriptSignIn = cfg_.scriptingEnabled](const httplib::Request& request,
                                                                             httplib::Response& response) {
    if (scriptSignIn && oauthApi_->handle(request, response)) return true;
    return voice_ && voice_->handle(request, response);
  };
  analysis_.emplace(speaker_.get(), supervised ? &*microphone_ : nullptr);
  app_->configureScripts(scriptServices_);
  scriptServices_.audioStats = [this](int64_t now, awtrix::audio::FrameStats& out) {
    return analysis_->analysis(static_cast<audio::AnalysisSource>(engine().state().settings().musicSource), now,
                               out);
  };
  scriptServices_.http = &scriptHttp_;
  scriptServices_.mqtt = mqttAllowed_ ? &scriptMqtt_ : nullptr;
}

void LinuxRuntime::composeBluetooth() {
  if (!o_.bluetooth) return;
  ble::BleService::Options bleOptions;
  bleOptions.bondsPath = host::hostPath("/ble-bonds.json");
  bleOptions.gamepadPath = host::hostPath("/gamepad.json");
  bleOptions.name = cfg_.hostname.empty() ? "AWTRIX NG" : cfg_.hostname;
  bleOptions.supervised = o_.supervised;
  bleOptions.gamepad = cfg_.scriptingEnabled;
  bleService_ = std::make_unique<ble::BleService>(bleOptions, [this](ble::BleEvent e) {
    if (extensions_.ble) extensions_.ble->push(std::move(e));
  });
  if (cfg_.scriptingEnabled) {
    extensions_.ble = std::make_unique<ble::BleScripting>(*bleService_);
    extensions_.list.push_back(extensions_.ble.get());
  }
  supervision_->setOnBluetooth([this](const tc002::BluetoothStatus& status) {
    bleService_->controllerStatus(status.on, status.error);
  });
}

void LinuxRuntime::composeExtensions() {
  Extensions& x = extensions_;
  if (cfg_.scriptingEnabled) {
    x.gamepadInput = std::make_unique<ble::GamepadMux>(x.phones, bleService_ ? &bleService_->gamepad() : nullptr);
    x.gamepad = std::make_unique<ble::GamepadScripting>(*x.gamepadInput);
    x.list.push_back(x.gamepad.get());
  }
  if (cfg_.scriptingEnabled) {
    if (o_.physicalInput) {
      x.knob = std::make_unique<script::KnobScripting>([] { return monotonicMs(); },
          [this] { scriptServices_.application->restartTurn(); });
      x.list.push_back(x.knob.get());
    }
    if (o_.height > 8) {
      x.layout = std::make_unique<layout::LayoutScripting>(DisplayProfile{o_.width, o_.height, false},
                                                          layoutResources_, layoutBudget_);
      x.list.push_back(x.layout.get());
    }
    x.audio = std::make_unique<tc002::AudioScripting>(
        [this](int64_t now, double& beat) { return speaker_ && speaker_->songBeat(now, beat); },
        [this](int64_t now) { return audioPitch(now); }, [] { return monotonicMs(); },
        [this](script::SoundAction action, const std::string& json, const std::string& owner,
               std::string& error) { return scriptServices_.application->sound(action, json, owner, error); });
    x.list.push_back(x.audio.get());
    x.list.push_back(&x.crypto);
    x.list.push_back(&x.tcp);
    x.list.push_back(&*oauthScripting_);
    oauthService_->dropStale();
    oauthService_->begin();
  }
  x.gamepadApi = std::make_unique<ble::GamepadApi>(bleService_ ? &bleService_->gamepad() : nullptr,
      cfg_.scriptingEnabled ? &x.phones : nullptr,
      [this] { return bleService_ ? bleService_->pairGamepad() : ble::GamepadPairResult{}; },
      [this](int id) { if (bleService_) bleService_->forgetGamepad(id); });
  if (bleService_) {
    iphone::IphoneBridge::Options iphoneOptions;
    iphoneOptions.panel.width = o_.width;
    iphoneOptions.panel.height = o_.height;
    iphoneOptions.panel.layouts = o_.height > 8;
    iphoneOptions.panel.appFont = awtrixFontCatalog().find(iphone::kAppFont) != nullptr;
    iphoneOptions.path = host::hostPath("/iphone.json");
    iphoneOptions.log = [](const std::string& line) { logf("%s", line.c_str()); };
    coverFetch_.begin();
    iphoneBridge_ = std::make_unique<iphone::IphoneBridge>(*bleService_, engine(), engine(), &coverFetch_,
                                                           std::move(iphoneOptions));
    bleService_->start();
  }
}

void LinuxRuntime::composePlatformRoutes() {
  httpOptions_.platformRoute = [this](const std::string& method, const std::string& path,
                                      const std::string& request, std::string& body) {
    if (path == "/api/v1/audio/clip")
      return speaker_ ? speaker_->routeAudio(method, path, request, body)
                      : tc002::routeClip<tc002::Tc002AudioSink>(method, path, request, body, nullptr, audio_);
    if (const int status = brokerTrustApi_ ? brokerTrustApi_->handle(method, path, request, body) : 0) return status;
    if (const int status = extensions_.gamepadApi->handle(method, path, request, body)) return status;
    return iphoneBridge_ ? iphoneBridge_->handle(method, path, request, body) : 0;
  };
}

void LinuxRuntime::startScripts() {
  bootstrap::configureScriptFiles(scriptServices_, store_, scriptIcon_);
  // Linux has no fixed heap: the VM gets its share of what is available at start, and a script's
  // buffers may grow into what stays available beyond the largest request body the server holds.
  std::uint64_t availableAtStart = 0;
  readMemAvailable(availableAtStart);
  const std::uint64_t bodyReserve = httpOptions_.maxBodyBytes;
  script::heap::configureHost("system", scriptHeapBudget(availableAtStart), [bodyReserve] {
    std::uint64_t available = 0;
    if (!readMemAvailable(available) || available <= bodyReserve) return std::size_t{0};
    return static_cast<std::size_t>(available - bodyReserve);
  });
  scriptServices_.freeHeap = [] {
    std::uint64_t available = 0;
    return readMemAvailable(available) ? static_cast<std::size_t>(available) : std::size_t{0};
  };
  scriptSlots_.begin(*app_, scriptServices_, cfg_.scriptingEnabled,
      [this, enabled = cfg_.scriptingEnabled](const std::string& name, const std::string& source) {
        store_.save(name, source);
        if (enabled) oauthService_->sourceSaved(name);
      },
      [this, enabled = cfg_.scriptingEnabled](const std::string& name) {
        store_.remove(name);
        if (enabled) oauthService_->sourceRemoved(name);
      });
  scripts_ = scriptSlots_.host();
  if (!scripts_) return;
  scriptExtensions_ = std::make_unique<script::ExtensionHost>(*scripts_, std::move(extensions_.list));
  bootstrap::bindScriptHttp(*scripts_, scriptHttp_);
  if (mqttAllowed_) {
    bootstrap::bindScriptMqtt(*scripts_, mqtt_, scriptMqtt_);
    mqtt_.setScriptingRunning(true);
  }
  bootstrap::restoreScripts(*scripts_, store_, 0);
}

int LinuxRuntime::startServices() {
  buttons_.begin(engine(), cfg_, &app_->launcher());
  webhook_.emplace(o_.uid);
  if (o_.physicalInput) webhook_->begin();
  bootstrap::bindScriptButtons(buttons_, engine(), scripts_);
  http_.setScripts(scripts_, scriptServices_.readSource, scriptServices_.readStore,
                   [this] { return store_.storedScripts(); });
  http_.setOnAssetsChanged([this] {
    bootstrap::assetsChanged(*app_, scriptIcon_);
    if (extensions_.layout) extensions_.layout->invalidateAssets();
  });
  // A sound stops before its file is deleted.
  http_.setReleaseFiles([this](const std::string& path) { audio_.release(path); });
  if (!http_.begin(static_cast<uint16_t>(o_.port), engine(), app_->canvas(), o_.uid, cfg_, o_.webui, httpOptions_)) {
    scriptHttp_.stop(); return 1;
  }
  const LinuxAdminConfig& administration = o_.administration;
  const std::string origin = administration.hardened ? administration.origin
                                                     : "http://" + listenAddress_ + ":" + std::to_string(o_.port);
  std::printf("AWTRIX NG %s %s @ %s (%dx%d)%s\n", AWTRIX_NG_VERSION, board().name(), origin.c_str(), o_.width,
              o_.height,
              administration.hardened ? (mqttAllowed_ ? " [authenticated HTTPS; verified MQTT TLS]"
                                                      : " [authenticated HTTPS; MQTT disabled]")
                                      : administration.lan ? " [LAN]" : "");
  std::fflush(stdout);
  startBootIntro();
  knobRouter_.emplace(Tc002KnobRouter::Targets{engine(), cfg_, buttons_, *webhook_, extensions_.knob.get(),
                                               knobGesture_, *quickSettings_, voice_.get()});
  bindPlatformCommands();
  return -1;
}

void LinuxRuntime::startBootIntro() {
  if (o_.bootIntro && !o_.bootSound.empty()) {
    const char* skipped = nullptr;
    std::error_code soundError;
    if (!speaker_ || !speaker_->available()) skipped = "no speaker";
    else if (!engine().state().settings().bootSound) skipped = "boot sound is off";
    else if (!std::filesystem::is_regular_file(std::filesystem::u8path(o_.bootSound), soundError))
      skipped = "no such file";
    else if (!speaker_->playBootSound(o_.bootSound, render::kBootSoundLeadFrames, engine().state().settings()))
      skipped = "speaker refused it";
    if (skipped) logf("boot: sound skipped (%s)", skipped);
    else bootSoundClock_ = std::make_unique<SpeakerBootSound>(*speaker_);
  }
  if (!o_.bootIntro) return;
  const int port = o_.port;
  intro_ = std::make_unique<Tc002BootIntro>(AWTRIX_NG_VERSION, [this, port]() -> std::string {
    const net::LinkStatus& wifi = engine().state().runtime().wifi;
    if (wifi.phase != net::LinkPhase::Connected || wifi.endpoint.empty()) return "";
    return port == 80 ? wifi.endpoint : wifi.endpoint + ":" + std::to_string(port);
  }, bootSoundClock_.get());
}

void LinuxRuntime::bindPlatformCommands() {
  if (!mqttAllowed_) return;
  if (o_.physicalInput) {
    mqttEntities_.assign(std::begin(ha::kKnobEntities), std::end(ha::kKnobEntities));
    engine().state().subscribe([this](StateEvent event) {
      if (event == StateEvent::ButtonsChanged) knobEvents_.observe(engine().state().runtime().knob);
    });
  }
  if (o_.supervised) mqttEntities_.push_back(kChargingEntity);
  if (voice_) mqttEntities_.push_back(tc002::voice::kStartButton);
  mqtt_.setPlatformCommands([this](const std::string& topic, const std::string&, std::string& result) {
    return voice_ && tc002::voice::handleMqtt(topic,
        [this] { return voiceAllowedNow() && startVoice(monotonicMs()); }, result);
  }, mqttEntities_.data(), mqttEntities_.size());
}

void LinuxRuntime::applyConfig() {
  ::setenv("TZ", cfg_.tz.c_str(), 1);
  ::tzset();
  logbuf::setVerbose(cfg_.debugMode);
}

bool LinuxRuntime::voiceAllowedNow() const {
  return !intro_ && !(update_ && update_->ownsPanel()) && !supervision_->accessPoint();
}

bool LinuxRuntime::startVoice(int64_t now) {
  if (!voice_->ready()) return false;
  quickSettings_->dismiss();
  return voice_->begin(now);
}

float LinuxRuntime::audioPitch(int64_t now) {
  return tc002::musicPitch(static_cast<audio::AnalysisSource>(engine().state().settings().musicSource), now,
                           speaker_ ? &speaker_->playbackPitch() : nullptr,
                           o_.supervised ? &*microphone_ : nullptr);
}

}
