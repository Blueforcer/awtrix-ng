#pragma once

#include <cstdint>
#include <functional>
#include <memory>
#include <string>

#include "core/StatePublishCadence.h"
#include "core/mqtt/ButtonEdges.h"
#include "persistence/DeviceConfig.h"
#include "transport/mqtt/HaAnnouncer.h"
#include "transport/mqtt/MqttLink.h"
#include "platform_mqtt/Commands.h"

namespace awtrix {

class CoreEngine;
class IBoard;
class ScriptMqttBridge;

class MqttService : private PlatformMqttCommands {
 public:
  void begin(CoreEngine& engine, IBoard& board, const DeviceConfig& cfg, const std::string& uid,
             const std::string& clientId, const std::string& hostname,
             net::IHostResolver& resolver, MqttSocket::Options transport = {});
  void tick();
  bool enabled() const { return link_.enabled(); }
  void publish(const std::string& suffix, const std::string& payload, bool retained = false);
  // Publishes api::errorEvent() output on event/error; empty events are ignored.
  void publishError(const std::string& event);
  void setCapabilitiesJson(std::shared_ptr<const std::string> j) {
    capabilitiesJson_ = std::move(j);
  }
  // The platform's device state builder, the same one /api/v1/device answers with.
  void setDeviceState(std::function<std::string(bool scriptingRunning)> build) {
    deviceState_ = std::move(build);
  }
  // The web UI address Home Assistant links the device to, asked before every announcement.
  void setWebUrl(std::function<std::string()> url) { webUrl_ = std::move(url); }
  void applyHaConfig(const DeviceConfig& cfg);
  // Commands only this platform has, asked after the shared ones with the topic below the prefix
  // ("cmd/..."): false for a topic it does not know, otherwise true with the result to publish.
  // entities are their Home Assistant counterparts and must outlive the service.
  using PlatformCommands =
      std::function<bool(const std::string& topic, const std::string& payload, std::string& result)>;
  void setPlatformCommands(PlatformCommands handle, const ha::Entity* entities, std::size_t count);

  void setScriptBridge(ScriptMqttBridge* b) { scriptBridge_ = b; }
  void setScriptingRunning(bool b) { scriptingRunning_ = b; }
  void publishRaw(const std::string& topic, const std::string& payload);
  void subscribeRaw(const std::string& topic);
  void unsubscribeRaw(const std::string& topic);

 private:
  void onOnline();
  void announceHa();
  bool send(const std::string& topic, const std::string& payload, bool retained);
  ha::ButtonEdges::State buttonState() const;
  void handleMessage(char* topic, uint8_t* payload, unsigned int len);
  static void onMessageStatic(char* topic, uint8_t* payload, unsigned int len);

  MqttLink link_;
  CoreEngine* engine_ = nullptr;
  IBoard* board_ = nullptr;
  std::string prefix_, uid_, hostname_;
  std::shared_ptr<const std::string> capabilitiesJson_ = std::make_shared<const std::string>("{}");
  std::function<std::string(bool)> deviceState_;
  std::function<std::string()> webUrl_;
  StatePublishCadence cadence_;
  ScriptMqttBridge* scriptBridge_ = nullptr;
  bool scriptingRunning_ = false;
  ha::ButtonEdges buttonEdges_;
  HaAnnouncer ha_;
};

}
