#include "transport/net/ScriptHostLink.h"

#include <cmath>
#include <ctime>
#include <new>
#include <utility>

#include "core/CoreEngine.h"
#include "core/api/ApiRouter.h"
#include "core/remote/HostAddress.h"
#include "core/remote/HostedScripts.h"
#include "core/remote/PairingKey.h"
#include "core/remote/RemoteClient.h"
#include "core/remote/ScriptHostStatus.h"
#include "core/script/ScriptServices.h"
#include "persistence/DeviceConfig.h"
#include "system/Log.h"
#include "system/MonotonicClock.h"
#include "transport/net/HmacSha256.h"
#include "transport/net/HostLookup.h"
#include "transport/net/UdpPort.h"

namespace awtrix {

namespace {
template <class T>
T clamped(float value, float low, float high) {
  if (!std::isfinite(value)) return static_cast<T>(low);
  return static_cast<T>(std::lround(value < low ? low : (value > high ? high : value)));
}
}

struct ScriptHostLink::Link final {
  Link(remote::HostedScripts& hosted, DeviceConfig& config, script::IScriptHttp& http)
      : hosted(hosted), config(config), http(http), client(udp, hosted.frames, hosted.apps) {}

  void tick(int64_t nowMs, bool online, CoreEngine& engine);
  void lookUp(int64_t nowMs);
  void report(CoreEngine& engine);
  void moveTo(uint32_t address, uint16_t port);
  const char* state() const;

  remote::HostedScripts& hosted;
  DeviceConfig& config;
  script::IScriptHttp& http;
  net::UdpPort udp;
  remote::RemoteClient client;
  net::HostLookup lookup;
  uint16_t hostPort = 0;
  uint32_t forwardId = 0xF0000000u;
  bool notFound = false;
};

ScriptHostLink::ScriptHostLink() = default;
ScriptHostLink::~ScriptHostLink() = default;

bool ScriptHostLink::begin(remote::HostedScripts& hosted, const std::string& uid, int width, int height,
                           uint16_t webPort, DeviceConfig& config, script::IScriptHttp& http, Random random,
                           OwnAddress ownAddress) {
  remote::PairingKey key;
  std::string hostName;
  uint16_t hostPort = 0;
  if (!remote::PairingKey::parse(config.scriptHostKey, key) || !remote::splitHost(config.scriptHost, hostName, hostPort))
    return false;
  link_.reset(new (std::nothrow) Link(hosted, config, http));
  if (!link_) return false;
  link_->client.begin(uid, width, height, webPort, ownAddress, remote::signerFor(key, hmacSha256), random);
  link_->lookup.setHost(std::move(hostName));
  link_->hostPort = hostPort;
  return true;
}

remote::HostActs* ScriptHostLink::acts() const { return link_ ? &link_->hosted.acts : nullptr; }

const remote::RemoteApps* ScriptHostLink::apps() const { return link_ ? &link_->hosted.apps : nullptr; }

void ScriptHostLink::settingsChanged() {
  if (link_) link_->client.settingsChanged();
}

void ScriptHostLink::systemChanged() {
  if (link_) link_->client.systemChanged();
}

void ScriptHostLink::assetsChanged() {
  if (link_) link_->client.assetsChanged();
}

bool ScriptHostLink::forward(const char* method, const std::string& path, std::string body) {
  if (!link_) return false;
  Link& l = *link_;
  script::HttpRequest req;
  req.id = ++l.forwardId;
  req.method = method;
  req.url = "http://" + l.lookup.host() + ":" + std::to_string(l.hostPort) + path;
  req.body = std::move(body);
  req.headers = {{"Content-Type", api::isRawBodyWrite(req.method, path) ? "text/plain" : "application/json"},
                 {"Authorization", "Bearer " + l.config.scriptHostKey}};
  req.maxBytes = 1024;
  req.timeoutSec = 15;
  return l.http.request(std::move(req));
}

void ScriptHostLink::tick(int64_t nowMs, bool online, CoreEngine& engine) {
  if (link_) link_->tick(nowMs, online, engine);
}

void ScriptHostLink::Link::tick(int64_t nowMs, bool online, CoreEngine& engine) {
  if (online) {
    udp.keepOpen(0, nowMs);
  } else if (udp.close()) {
    lookup.forget();
    client.dropLink();
  }
  if (!udp.isOpen()) return;
  net::Endpoint from;
  while (const std::size_t length = udp.receive(from)) client.receive(from, udp.data(), length, nowMs);
  lookUp(nowMs);
  report(engine);
  client.tick(engine.currentAppId(), engine.incomingAppId(), nowMs);
}

// The host is looked up again after half a minute without a link, in case it moved.
void ScriptHostLink::Link::lookUp(int64_t nowMs) {
  switch (lookup.poll(nowMs, client.hasHost(), client.linkUp())) {
    case net::HostLookup::Result::Idle:
    case net::HostLookup::Result::Pending:
      return;
    case net::HostLookup::Result::Failed:
      notFound = !client.hasHost();
      return;
    case net::HostLookup::Result::Found:
      notFound = false;
      client.setHost({lookup.address(), hostPort});
      return;
  }
}

void ScriptHostLink::Link::report(CoreEngine& engine) {
  const RuntimeState& rt = engine.state().runtime();
  remote::wire::Sensors sensors;
  if (rt.hasTemperature) {
    sensors.present |= remote::wire::sensor::kTemperature;
    sensors.temperatureCenti = clamped<int16_t>(rt.temperatureC * 100.0f, -32768.0f, 32767.0f);
  }
  if (rt.hasHumidity) {
    sensors.present |= remote::wire::sensor::kHumidity;
    sensors.humidityDeci = clamped<uint16_t>(rt.humidity * 10.0f, 0.0f, 65535.0f);
  }
  if (rt.hasPressure) {
    sensors.present |= remote::wire::sensor::kPressure;
    sensors.pressureDeci = clamped<uint16_t>(rt.pressureHpa * 10.0f, 0.0f, 65535.0f);
  }
  if (rt.hasLightSensor) {
    sensors.present |= remote::wire::sensor::kLight;
    sensors.light = clamped<uint16_t>(rt.lightLevel, 0.0f, 65535.0f);
  }
  if (rt.hasBattery) {
    sensors.present |= remote::wire::sensor::kBattery;
    sensors.batteryPercent = rt.batteryPercent;
    sensors.batteryMillivolts = clamped<uint16_t>(rt.batteryVoltage * 1000.0f, 0.0f, 65535.0f);
  }
  uint8_t state = rt.matrixOff ? 0 : remote::wire::state::kDisplayOn;
  if (engine.audio().appSoundPlaying()) state |= remote::wire::state::kAudioPlaying;
  if (engine.scriptRotationPaused()) state |= remote::wire::state::kRotationHeld;
  client.setState(state, sensors);
  client.setButtons(hosted.buttons.down(), hosted.buttons.presses());
}

void ScriptHostLink::Link::moveTo(uint32_t address, uint16_t port) {
  lookup.setHost(remote::addressText(address));
  hostPort = port;
  notFound = false;
  config.scriptHost = remote::hostSpec(address, port);
  config.save();
  logf("scripts: script host moved to %s", config.scriptHost.c_str());
}

void ScriptHostLink::discovery(const uint8_t* data, std::size_t length, uint32_t fromAddress) {
  remote::wire::Packet packet;
  if (!remote::wire::decode(data, length, packet) || packet.type != remote::wire::Type::Offer) return;
  const int64_t nowMs = monotonicMs();
  offers_.offer(packet.offer, fromAddress, nowMs);
  if (link_ && link_->client.follow(packet, fromAddress, static_cast<int64_t>(std::time(nullptr))))
    link_->moveTo(fromAddress, packet.offer.port);
}

const char* ScriptHostLink::Link::state() const {
  if (notFound) return "notFound";
  if (!client.hasHost()) return "resolving";
  return client.linkUp() ? "up" : "down";
}

std::string ScriptHostLink::statusJson(const DeviceConfig& config, bool withKey) const {
  remote::ScriptHostStatus status;
  status.host = config.scriptHost;
  if (withKey) status.key = config.scriptHostKey;
  status.link = link_ ? link_->state() : "off";
  status.offers = offers_.current(monotonicMs());
  return remote::scriptHostJson(status);
}

}
