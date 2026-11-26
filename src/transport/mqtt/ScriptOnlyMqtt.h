#pragma once

#include <cstdint>
#include <string>

#include "core/net/LinkStatus.h"
#include "persistence/DeviceConfig.h"
#include "transport/ScriptMqttBridge.h"
#include "transport/mqtt/MqttLink.h"

namespace awtrix {

namespace script {
class ScriptHost;
}

// MQTT for nothing but scripts: the script host of a clock connects to the clock's broker with its
// own client id and prefix, carries the scripts' subscriptions and messages, and leaves the
// clock's own topics alone - no commands, no state, no Home Assistant discovery.
class ScriptOnlyMqtt {
 public:
  ScriptOnlyMqtt() = default;
  ~ScriptOnlyMqtt();
  ScriptOnlyMqtt(const ScriptOnlyMqtt&) = delete;
  ScriptOnlyMqtt& operator=(const ScriptOnlyMqtt&) = delete;

  // broker: mqttEnabled, mqttHost, mqttPort, mqttUser and mqttPass are read.
  void begin(const DeviceConfig& broker, const std::string& clientId, const std::string& prefix,
             net::IHostResolver& resolver, script::ScriptHost& scripts, ScriptMqttBridge& bridge);
  void tick(int64_t nowMs, bool networkConnected);

 private:
  static void onMessage(char* topic, uint8_t* payload, unsigned int length);
  static ScriptOnlyMqtt* active_;

  MqttLink link_;
  net::LinkStatus status_;
  ScriptMqttBridge* bridge_ = nullptr;
  std::string prefix_;
};

}
