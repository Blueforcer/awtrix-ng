#include "platform/linux/remote/RemoteRole.h"

#include <algorithm>
#include <cstring>
#include <filesystem>
#include <random>

#include "core/ApplicationRuntime.h"
#include "core/Command.h"
#include "core/api/JsonReader.h"
#include "core/api/JsonWriter.h"
#include "core/net/Url.h"
#include "core/remote/HostActs.h"
#include "core/remote/HostAddress.h"
#include "core/remote/PairingKey.h"
#include "core/script/ScriptHost.h"
#include "platform/linux/host/HostStore.h"
#include "platform/linux/remote/RemoteClockApplication.h"
#include "platform/posix/Files.h"
#include "system/Log.h"
#include "transport/net/HmacSha256.h"

namespace awtrix::remote {

namespace {
uint64_t stamp(uint32_t connection, uint16_t revision) {
  return static_cast<uint64_t>(connection) << 16 | revision;
}

bool plainIconName(const std::string& name) {
  if (name.empty() || name.size() > 64 || name.find("..") != std::string::npos) return false;
  for (const char c : name)
    if (!((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '_' || c == '-' ||
          c == '.'))
      return false;
  return true;
}

bool keep(const std::string& path, const std::string& body) {
  std::string held;
  if (path.empty() || (host::readFile(path, held) && held == body)) return false;
  return host::writeFile(path, body);
}

bool drop(const std::string& path) {
  std::error_code error;
  return !path.empty() && std::filesystem::remove(path, error);
}
}

bool RemoteRole::begin(const RoleOptions& options, uint16_t port, int width, int height, std::string& error) {
  std::string text;
  PairingKey key;
  if (!posix::readText(options.keyFile, text) || !PairingKey::parse(keyHex_ = posix::trimmed(text), key)) {
    error = "--remote-key-file must hold 64 hex digits";
    return false;
  }
  const auto url = net::parseUrl(options.clockUrl);
  if (!url || url->tls() || url->userinfo) {
    error = "--remote-clock-url must be http://address[:port]";
    return false;
  }
  clockHost_ = std::string(url->host);
  clockPort_ = url->effectivePort;
  current_.reset(new Canvas(width, height));
  incoming_.reset(new Canvas(width, height));
  port_.keepOpen(port, 0);
  if (!current_->valid() || !incoming_->valid() || !port_.isOpen()) {
    error = "cannot open UDP port " + std::to_string(port);
    return false;
  }
  clock_.reset(new ClockApi(url->origin(), keyHex_));
  server_.reset(new RemoteServer(port_, options.uid, [key](const wire::Packet& hello) {
    return signedBy(hello, key, hmacSha256);
  }, static_cast<uint16_t>(std::random_device{}())));
  return true;
}

void RemoteRole::act(std::string body, std::function<void()> answered) {
  clock_->request("POST", kActPath, std::move(body),
                  [this, answered = std::move(answered)](int status, const std::string& answer) {
    const bool ok = status >= 200 && status < 300;
    if (!ok && status != lastActStatus_) logf("script host: the clock refused an act (%d) %s", status, answer.c_str());
    lastActStatus_ = ok ? 200 : status;
    if (answered) answered();
  });
}

bool RemoteRole::clockHolds() const {
  const ClockState& clock = server_->clock();
  if (holdOut_ || clock.counter == holdAnsweredAt_) return holdAsked_;
  return (clock.state & wire::state::kRotationHeld) != 0;
}

void RemoteRole::hold(bool on) {
  if (on == clockHolds()) return;
  holdAsked_ = on;
  holdOut_ = true;
  std::string body;
  api::JsonWriter(body).beginObject().key("act").value("hold").key("hold").value(on).endObject();
  act(std::move(body), [this] {
    holdOut_ = false;
    holdAnsweredAt_ = server_->clock().counter;
  });
}

void RemoteRole::bindIcons(ScriptIcon& icons) {
  icons.setOnMissing(this);
  icons_ = &icons;
  std::error_code error;
  for (auto it = std::filesystem::directory_iterator(host::hostPath("/ICONS"), error);
       !error && it != std::filesystem::directory_iterator(); it.increment(error)) {
    const std::string extension = it->path().extension().string();
    const std::string name = it->path().stem().string();
    if ((extension == ".gif" || extension == ".jpg") && plainIconName(name)) iconNames_.insert(name);
  }
}

void RemoteRole::bindScripts(script::ScriptHost& scripts, CoreEngine& engine) {
  scripts.setSwitchedOff([this, &engine](const std::string& id) { return !switchesApplied_ || !engine.isEnabled(id); });
}

void RemoteRole::iconMissing(const std::string& name) {
  if (plainIconName(name) && iconNames_.insert(name).second) queueIcon(name);
}

void RemoteRole::queueIcon(const std::string& name) {
  const auto named = [&name](const WantedIcon& icon) { return icon.name == name; };
  if (std::none_of(iconQueue_.begin(), iconQueue_.end(), named)) iconQueue_.push_back({name});
}

void RemoteRole::nextIcon() {
  if (iconAsking_) return;
  const auto due = std::find_if(iconQueue_.begin(), iconQueue_.end(),
                                [this](const WantedIcon& icon) { return nowMs_ >= icon.retryAtMs; });
  if (due == iconQueue_.end()) return;
  iconAsking_ = true;
  fetchIcon(due->name, ".gif");
}

void RemoteRole::fetchIcon(const std::string& name, const char* extension) {
  clock_->request("GET", "/ICONS/" + name + extension, {}, [this, name, extension](int status, const std::string& body) {
    iconDone(name, extension, status, body);
  });
}

void RemoteRole::iconDone(const std::string& name, const char* extension, int status, const std::string& body) {
  const bool gif = std::strcmp(extension, ".gif") == 0;
  if (gif && status == 404) {
    fetchIcon(name, ".jpg");
    return;
  }
  iconAsking_ = false;
  const auto asked = std::find_if(iconQueue_.begin(), iconQueue_.end(),
                                  [&name](const WantedIcon& icon) { return icon.name == name; });
  if (status != 200 && status != 404) {
    if (asked != iconQueue_.end()) asked->retryAtMs = nowMs_ + kRetryMs;
    return;
  }
  if (asked != iconQueue_.end()) iconQueue_.erase(asked);
  const std::string gifPath = host::hostPath("/ICONS/" + name + ".gif");
  const std::string jpgPath = host::hostPath("/ICONS/" + name + ".jpg");
  if (status == 200 && !body.empty()) {
    const bool written = keep(gif ? gifPath : jpgPath, body);
    const bool dropped = drop(gif ? jpgPath : gifPath);
    iconsChanged_ = iconsChanged_ || written || dropped;
  } else if (status == 404) {
    const bool droppedGif = drop(gifPath);
    const bool droppedJpg = drop(jpgPath);
    iconsChanged_ = iconsChanged_ || droppedGif || droppedJpg;
  }
}

void RemoteRole::followClock() {
  const ClockState& clock = server_->clock();
  const net::Endpoint at{clock.address, clock.webPort};
  if (at == clockAt_) return;
  clockAt_ = at;
  clock_->setOrigin("http://" + (at.address ? addressText(at.address) : clockHost_) + ":" +
                    std::to_string(at.port ? at.port : clockPort_));
  logf("script host: the clock answers at %s", clock_->origin().c_str());
}

void RemoteRole::takeReadings(ApplicationRuntime& app) {
  const wire::Sensors& s = server_->clock().sensors;
  RuntimeState& rt = app.engine().state().runtime();
  rt.hasTemperature = s.present & wire::sensor::kTemperature;
  rt.hasHumidity = s.present & wire::sensor::kHumidity;
  rt.hasPressure = s.present & wire::sensor::kPressure;
  rt.hasLightSensor = s.present & wire::sensor::kLight;
  rt.hasBattery = s.present & wire::sensor::kBattery;
  rt.temperatureC = s.temperatureCenti / 100.0f;
  rt.humidity = s.humidityDeci / 10.0f;
  rt.pressureHpa = s.pressureDeci / 10.0f;
  rt.lightLevel = s.light;
  rt.batteryPercent = s.batteryPercent;
  rt.batteryVoltage = s.batteryMillivolts / 1000.0f;
  rt.matrixOff = !(server_->clock().state & wire::state::kDisplayOn);
}

// Each copy is fetched again on a fresh connection and when its revision on the clock moves; the
// icons this host knows are fetched again when the clock's files change.
void RemoteRole::sync(int64_t nowMs, CoreEngine& engine) {
  const ClockState& clock = server_->clock();
  const uint32_t connection = server_->connection();
  const uint64_t caps = stamp(connection, 0);
  if (due(caps_, caps, nowMs))
    fetch(caps_, caps, "/api/v1/capabilities", [this](const std::string& body) { soundCaps_ = soundCapsFrom(body); });
  const uint64_t settings = stamp(connection, clock.settingsRevision);
  if (due(settings_, settings, nowMs))
    fetch(settings_, settings, "/api/v1/settings", [&engine](const std::string& body) {
      Command command(CommandType::SetSettings);
      command.payload = body;
      command.source = Source::Internal;
      if (engine.execute(command) != DispatchResult::Ok)
        logf("script host: the clock's settings did not apply: %s", engine.lastDetail().message.c_str());
    });
  const uint64_t system = stamp(connection, clock.systemRevision);
  if (due(system_, system, nowMs))
    fetch(system_, system, "/api/v1/system?secrets=1", [this](const std::string& body) { applySystem(body); });
  const uint64_t assets = stamp(connection, clock.assetsRevision);
  if (assets != assets_) {
    assets_ = assets;
    for (const std::string& name : iconNames_) queueIcon(name);
  }
  nextIcon();
}

bool RemoteRole::due(const Copy& copy, uint64_t wanted, int64_t nowMs) {
  return !copy.asking && copy.applied != wanted && nowMs >= copy.retryAtMs;
}

void RemoteRole::fetch(Copy& copy, uint64_t wanted, const char* path, std::function<void(const std::string&)> apply) {
  copy.asking = true;
  clock_->request("GET", path, {}, [this, &copy, wanted, apply = std::move(apply)](int status, const std::string& body) {
    copy.asking = false;
    if (status != 200) {
      copy.retryAtMs = nowMs_ + kRetryMs;
      return;
    }
    copy.applied = wanted;
    apply(body);
  });
}

void RemoteRole::applySystem(const std::string& system) {
  if (onTimezone_) onTimezone_(api::memberText(system, "tz"), api::memberText(system, "tzName"));
  Broker broker;
  api::memberValue(system, "mqttEnabled").asBool(broker.enabled);
  broker.host = api::memberText(system, "mqttHost");
  broker.user = api::memberText(system, "mqttUser");
  broker.pass = api::memberText(system, "mqttPass");
  broker.prefix = api::memberText(system, "mqttPrefix");
  long long port = 1883;
  if (api::memberValue(system, "mqttPort").asLong(port) && port > 0 && port < 65536) broker.port = static_cast<long>(port);
  if (haveBroker_ && broker == broker_) return;
  haveBroker_ = true;
  broker_ = broker;
  if (onBroker_) onBroker_(broker_);
}

void RemoteRole::offerApps(int64_t nowMs, script::ScriptHost& scripts) {
  if (nowMs - appsAtMs_ < kOfferEveryMs) return;
  appsAtMs_ = nowMs;
  std::vector<HostApp> apps;
  offered_.clear();
  for (const auto& [name, info] : scripts.list()) {
    if (info.module) continue;
    HostApp app;
    app.name = name;
    if (info.headless) app.flags |= wire::app::kHeadless;
    if (info.onDemand) app.flags |= wire::app::kOnDemand;
    else offered_.push_back(name);
    if (info.error.empty() && scripts.takesButtons(name)) app.flags |= wire::app::kTakesButtons;
    if (scripts.wantsShow(name)) app.flags |= wire::app::kWantsShow;
    if (scripts.scrollHolds(name)) app.flags |= wire::app::kScrollHolds;
    const long duration = scripts.durationMs(name);
    app.durationMs = duration > 0 ? static_cast<uint32_t>(duration) : 0;
    apps.push_back(std::move(app));
  }
  server_->setApps(std::move(apps), nowMs);
}

void RemoteRole::followSwitches(CoreEngine& engine, script::ScriptHost& scripts) {
  if (!server_->switchesKnown()) return;
  const std::vector<std::string>& off = server_->switchedOff();
  if (switchesApplied_ && off == appliedOff_) return;
  const bool first = !switchesApplied_;
  switchesApplied_ = true;
  appliedOff_ = off;
  for (const std::string& name : offered_)
    engine.setAppEnabled(name, std::find(off.begin(), off.end(), name) == off.end());
  if (first) scripts.recheckSwitches();
}

void RemoteRole::followSession(int64_t nowMs, CoreEngine& engine, script::ScriptHost& scripts) {
  const std::string& incoming = server_->incoming();
  const std::string& asked = !incoming.empty() && scripts.isOnDemand(incoming) ? incoming : server_->current();
  if (!asked.empty() && scripts.isOnDemand(asked)) {
    sessionAskedMs_ = nowMs;
    if (engine.sessionApp() != asked && nowMs >= sessionRetryAtMs_) {
      DispatchDetail detail;
      if (engine.startSession(asked, detail) != DispatchResult::Ok) {
        sessionRetryAtMs_ = nowMs + kRetryMs;
        logf("script host: %s did not start: %s", asked.c_str(), detail.message.c_str());
      }
    }
  }
  if (engine.inSession() && nowMs - sessionAskedMs_ > kSessionLingerMs) engine.endSession();
}

void RemoteRole::pressButtons(script::ScriptHost& scripts) {
  bool pressed[wire::kButtons] = {};
  server_->stepButtons(pressed);
  const std::string& current = server_->current();
  for (int i = 0; i < wire::kButtons; ++i) {
    const bool edge = pressed[i] && !down_[i];
    down_[i] = pressed[i];
    if (current.empty()) continue;
    const bool taken = scripts.handleButtonState(current, i, pressed[i], pressed[i]);
    if (!edge || taken) continue;
    std::string body;
    api::JsonWriter(body).beginObject().key("act").value("press").key("app").value(current).key("button").value(i)
        .endObject();
    act(std::move(body));
  }
}

void RemoteRole::draw(ApplicationRuntime& app, const std::string& name, Canvas& canvas, const RenderCtx& ctx,
                      int64_t nowMs) {
  if (name.empty()) return;
  IApp* shown = app.apps().find(name);
  if (!shown) return;
  canvas.clear();
  shown->render(canvas, ctx);
  server_->sendFrame(name, canvas, nowMs);
}

void RemoteRole::tick(int64_t nowMs, ApplicationRuntime& app, const ApplicationServices& services,
                      script::ScriptHost* scripts) {
  nowMs_ = nowMs;
  clock_->poll();
  if (iconsChanged_ && icons_) icons_->invalidate();
  iconsChanged_ = false;
  net::Endpoint from;
  while (const std::size_t length = port_.receive(from)) server_->receive(from, port_.data(), length, nowMs);
  server_->tick(nowMs);
  const bool connected = server_->connected();
  if (connected) {
    followClock();
    takeReadings(app);
    sync(nowMs, app.engine());
  }
  if (!scripts) return;
  offerApps(nowMs, *scripts);
  if (connected) followSwitches(app.engine(), *scripts);
  followSession(nowMs, app.engine(), *scripts);
  RenderCtx ctx;
  ctx.settings = &app.engine().state().settings();
  ctx.runtime = &app.engine().state().runtime();
  ctx.font = services.fonts.small().font;
  ctx.fonts = &services.fonts;
  services.clock.fill(ctx, nowMs);
  const std::string& current = server_->current();
  const std::string& incoming = server_->incoming();
  scripts->tick(ctx, current, incoming);
  pressButtons(*scripts);
  if (!(server_->clock().state & wire::state::kDisplayOn)) return;
  draw(app, current, *current_, ctx, nowMs);
  draw(app, incoming, *incoming_, ctx, nowMs);
}

}
