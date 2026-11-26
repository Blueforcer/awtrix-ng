#include "transport/mqtt/ScriptOnlyMqtt.h"

#include "core/script/ScriptHost.h"

namespace awtrix {

ScriptOnlyMqtt* ScriptOnlyMqtt::active_ = nullptr;

ScriptOnlyMqtt::~ScriptOnlyMqtt() {
  if (active_ == this) active_ = nullptr;
  if (bridge_) bridge_->begin({}, {}, {}, {});
}

void ScriptOnlyMqtt::onMessage(char* topic, uint8_t* payload, unsigned int length) {
  if (active_ && active_->bridge_) active_->bridge_->offer(topic, payload, length);
}

void ScriptOnlyMqtt::begin(const DeviceConfig& broker, const std::string& clientId, const std::string& prefix,
                           net::IHostResolver& resolver, script::ScriptHost& scripts, ScriptMqttBridge& bridge) {
  prefix_ = prefix;
  link_.begin(broker, clientId, prefix, &resolver, &status_);
  if (!link_.enabled()) return;
  active_ = this;
  bridge_ = &bridge;
  link_.client()->setCallback(onMessage);
  link_.setOnOnline([this] {
    link_.client()->publish((prefix_ + "/availability").c_str(), "online", true);
    bridge_->onReconnect();
  });
  bridge.begin(
      [this](const std::string& topic, const std::string& body) {
        if (link_.online()) link_.publish(topic, body, false);
      },
      [this](const std::string& topic) {
        if (link_.online()) link_.client()->subscribe(topic.c_str());
      },
      [this](const std::string& topic) {
        if (link_.online()) link_.client()->unsubscribe(topic.c_str());
      },
      [&scripts](script::MqttMessage message) { scripts.pushMqttMessage(std::move(message)); });
}

void ScriptOnlyMqtt::tick(int64_t nowMs, bool networkConnected) {
  if (link_.enabled()) link_.tick(static_cast<uint32_t>(nowMs), networkConnected);
}

}
