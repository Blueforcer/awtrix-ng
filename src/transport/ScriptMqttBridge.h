#pragma once

#include <cstddef>
#include <cstdint>
#include <functional>
#include <string>
#include <vector>

#include "core/script/ScriptServices.h"

namespace awtrix {

class ScriptMqttBridge : public script::IScriptMqtt {
 public:
  using PublishFn = std::function<void(const std::string& topic, const std::string& payload)>;
  using SubscribeFn = std::function<void(const std::string& topic)>;
  using UnsubscribeFn = std::function<void(const std::string& topic)>;
  using MessageFn = std::function<void(script::MqttMessage)>;

  void begin(PublishFn pub, SubscribeFn sub, UnsubscribeFn unsub, MessageFn onMessage);

  void publish(const std::string& topic, const std::string& payload) override;
  void subscribe(const std::string& topic) override;
  void unsubscribeAll(const std::string& topic) override;

  bool offer(const char* topic, const uint8_t* payload, unsigned len);

  void onReconnect();

  std::size_t subscriptionCount() const { return topics_.size(); }

 private:
  static constexpr std::size_t kMaxTopics = 32;

  PublishFn pub_;
  SubscribeFn sub_;
  UnsubscribeFn unsub_;
  MessageFn onMessage_;
  std::vector<std::string> topics_;
};

}
