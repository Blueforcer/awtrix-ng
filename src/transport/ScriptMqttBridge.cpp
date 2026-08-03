#include "transport/ScriptMqttBridge.h"

#include <algorithm>
#include <utility>

#include "transport/MqttTopicMatch.h"

namespace awtrix {

void ScriptMqttBridge::begin(PublishFn pub, SubscribeFn sub, UnsubscribeFn unsub,
                             MessageFn onMessage) {
  pub_ = std::move(pub);
  sub_ = std::move(sub);
  unsub_ = std::move(unsub);
  onMessage_ = std::move(onMessage);
}

void ScriptMqttBridge::publish(const std::string& topic, const std::string& payload) {
  if (topic.empty()) return;
  if (pub_) pub_(topic, payload);
}

void ScriptMqttBridge::subscribe(const std::string& topic) {
  if (topic.empty()) return;
  if (std::find(topics_.begin(), topics_.end(), topic) != topics_.end()) return;
  if (topics_.size() >= kMaxTopics) return;
  topics_.push_back(topic);
  if (sub_) sub_(topic);
}

void ScriptMqttBridge::unsubscribeAll(const std::string& topic) {
  const auto it = std::find(topics_.begin(), topics_.end(), topic);
  if (it == topics_.end()) return;
  topics_.erase(it);
  if (unsub_) unsub_(topic);
}

// Offered every inbound message, including the device's own command topics. A message matching
// several script filters is delivered once per filter, each tagged with the filter that matched.
bool ScriptMqttBridge::offer(const char* topic, const uint8_t* payload, unsigned len) {
  if (!topic || topics_.empty() || !onMessage_) return false;
  const std::string concrete(topic);
  if (concrete.empty()) return false;

  std::string body;
  if (payload && len) body.assign(reinterpret_cast<const char*>(payload), len);

  bool matched = false;
  for (const auto& filter : topics_) {
    if (!mqtt::topicMatches(filter, concrete)) continue;
    matched = true;
    onMessage_(script::MqttMessage{concrete, body, filter});
  }
  return matched;
}

// Subscriptions live on the broker, so a reconnect silently loses every script subscription unless
// they are all replayed.
void ScriptMqttBridge::onReconnect() {
  if (!sub_) return;
  for (const auto& topic : topics_) sub_(topic);
}

}
