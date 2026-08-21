#include "transport/mqtt/MqttService.h"

#include "core/CoreEngine.h"
#include "core/api/ApiRouter.h"
#include "system/Log.h"
#include "system/MonotonicClock.h"
#include "transport/DeviceStateJson.h"
#include "transport/ScriptMqttBridge.h"

namespace awtrix {

// PubSubClient takes a plain function pointer for its callback, so the single live instance has to
// be reachable through a file-scope pointer.
namespace {
MqttService* s_self = nullptr;
}

void MqttService::onMessageStatic(char* topic, uint8_t* payload, unsigned int len) {
  if (s_self) s_self->handleMessage(topic, payload, len);
}

void MqttService::begin(CoreEngine& engine, IBoard& board, const DeviceConfig& cfg,
                        const std::string& uid, const std::string& clientId,
                        const std::string& hostname, net::IHostResolver& resolver) {
  engine_ = &engine;
  board_ = &board;
  uid_ = uid;
  prefix_ = cfg.mqttPrefix.empty() ? uid : cfg.mqttPrefix;
  hostname_ = hostname;
  cadence_.configure(cfg.statsInterval);
  s_self = this;

  link_.begin(cfg, clientId, prefix_, &resolver, &engine.state().runtime().mqtt);
  if (!link_.enabled()) return;

  ha_.configure(cfg, board, uid, prefix_, hostname_);
  engine_->state().subscribe([this](StateEvent e) { cadence_.onEvent(e); });
  link_.setOnOnline([this] { onOnline(); });
  link_.client()->setCallback(onMessageStatic);
}

// Runs after every successful connect, not just the first. Subscriptions and retained state do not
// survive a broker restart, so they all have to be re-sent here.
void MqttService::onOnline() {
  PubSubClient* client = link_.client();
  client->subscribe((prefix_ + "/cmd/#").c_str());
  client->publish((prefix_ + "/availability").c_str(), "online", true);
  send(prefix_ + "/state/capabilities", *capabilitiesJson_, true);
  send(prefix_ + "/state/prefix", prefix_, true);
  ha_.announce(*client);
  cadence_.onConnect();
  if (scriptBridge_) scriptBridge_->onReconnect();
}

void MqttService::tick() {
  const int64_t now = monotonicMs();
  if (!link_.tick(static_cast<uint32_t>(now))) return;

  const std::string& app = engine_->currentAppId();
  if (cadence_.appDue(app)) publish("state/apps/active", app, true);
  if (cadence_.buttonsDue()) {
    static const char* kNames[3] = {"left", "select", "right"};
    const auto& buttons = engine_->state().runtime().buttons;
    for (int i = 0; i < 3; ++i)
      publish(std::string("state/buttons/") + kNames[i], buttons[i] ? "1" : "0", false);
  }
  if (cadence_.settingsDue()) publish("state/settings", buildSettingsJson(*engine_), true);
  if (cadence_.radioDue()) publish("state/audio", buildAudioJson(*engine_), true);
  if (cadence_.stateDue(now))
    publish("state/device", buildDeviceStateJson(*engine_, *board_, uid_, scriptingRunning_),
            true);
}

// Publishes in streaming form so the payload is never copied into the packet buffer; that keeps
// state documents larger than the buffer's spare room from failing.
bool MqttService::send(const std::string& topic, const std::string& payload, bool retained) {
  PubSubClient* client = link_.client();
  if (!client->beginPublish(topic.c_str(), static_cast<unsigned int>(payload.size()), retained))
    return false;
  if (!payload.empty())
    client->write(reinterpret_cast<const uint8_t*>(payload.data()), payload.size());
  return client->endPublish() != 0;
}

void MqttService::publish(const std::string& suffix, const std::string& payload, bool retained) {
  if (!link_.online()) return;
  send(prefix_ + "/" + suffix, payload, retained);
}

void MqttService::publishRaw(const std::string& topic, const std::string& payload) {
  if (!link_.online()) return;
  send(topic, payload, false);
}

void MqttService::subscribeRaw(const std::string& topic) {
  if (!link_.online()) return;
  link_.client()->subscribe(topic.c_str());
}

void MqttService::unsubscribeRaw(const std::string& topic) {
  if (!link_.online()) return;
  link_.client()->unsubscribe(topic.c_str());
}

void MqttService::applyHaConfig(const DeviceConfig& cfg) {
  if (!link_.enabled() || !board_) return;
  ha_.configure(cfg, *board_, uid_, prefix_, hostname_);
  if (link_.online()) ha_.announce(*link_.client());
}

// Reached from inside link_.tick() via PubSubClient::loop(), so this is still the main loop and
// commands may be executed straight away. topic and payload point into the client's buffer.
void MqttService::handleMessage(char* topic, uint8_t* payload, unsigned int len) {
  if (scriptBridge_) scriptBridge_->offer(topic, payload, len);

  const std::string t(topic);
  const std::string pfx = prefix_ + "/";
  if (t.rfind(pfx, 0) != 0) return;
  const std::string suffix = t.substr(pfx.size());
  // We publish our own replies under the same prefix and are subscribed to it, so skip them or
  // every command answers itself forever.
  if (api::isResultEcho(suffix)) return;
  const std::string body(reinterpret_cast<char*>(payload), len);
  engine_->state().runtime().receivedMessages++;
  Command cmd;
  std::string result;
  switch (api::routeMqtt(suffix, body, cmd, result)) {
    case api::RouteOutcome::Routed: {
      const DispatchResult r = engine_->execute(cmd);
      logdbg("mqtt cmd %s -> %d", suffix.c_str(), static_cast<int>(r));
      publish(suffix + "/result", api::mqttResult(r, engine_->lastDetail()));
      break;
    }
    case api::RouteOutcome::Respond:
      publish(suffix + "/result", result);
      break;
    case api::RouteOutcome::NoMatch:
    default:
      break;
  }
}

}
