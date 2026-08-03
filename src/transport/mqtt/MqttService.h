#pragma once

#include <cstdint>
#include <memory>
#include <string>

#include "core/StatePublishCadence.h"
#include "persistence/DeviceConfig.h"
#include "transport/mqtt/HaAnnouncer.h"
#include "transport/mqtt/MqttLink.h"

namespace awtrix {

class CoreEngine;
class IBoard;
class ScriptMqttBridge;

class MqttService {
 public:
  void begin(CoreEngine& engine, IBoard& board, const DeviceConfig& cfg, const std::string& uid,
             const std::string& clientId, const std::string& hostname,
             net::IHostResolver& resolver);
  void tick();
  bool enabled() const { return link_.enabled(); }
  void publish(const std::string& suffix, const std::string& payload, bool retained = false);
  void setCapabilitiesJson(std::shared_ptr<const std::string> j) {
    capabilitiesJson_ = std::move(j);
  }
  void applyHaConfig(const DeviceConfig& cfg);

  void setScriptBridge(ScriptMqttBridge* b) { scriptBridge_ = b; }
  void setScriptingRunning(bool b) { scriptingRunning_ = b; }
  void publishRaw(const std::string& topic, const std::string& payload);
  void subscribeRaw(const std::string& topic);
  void unsubscribeRaw(const std::string& topic);

 private:
  void onOnline();
  bool send(const std::string& topic, const std::string& payload, bool retained);
  void handleMessage(char* topic, uint8_t* payload, unsigned int len);
  static void onMessageStatic(char* topic, uint8_t* payload, unsigned int len);

  MqttLink link_;
  CoreEngine* engine_ = nullptr;
  IBoard* board_ = nullptr;
  std::string prefix_, uid_, hostname_;
  std::shared_ptr<const std::string> capabilitiesJson_ = std::make_shared<const std::string>("{}");
  StatePublishCadence cadence_;
  ScriptMqttBridge* scriptBridge_ = nullptr;
  bool scriptingRunning_ = false;
  HaAnnouncer ha_;
};

}
