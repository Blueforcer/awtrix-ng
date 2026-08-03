#include <unity.h>

#include <string>
#include <vector>

#include "transport/ScriptMqttBridge.h"

using namespace awtrix;

namespace {

struct Transport {
  std::vector<std::pair<std::string, std::string>> published;
  std::vector<std::string> subscribed;
  std::vector<std::string> unsubscribed;
  std::vector<script::MqttMessage> delivered;

  void wire(ScriptMqttBridge& b) {
    b.begin([this](const std::string& t, const std::string& p) { published.push_back({t, p}); },
            [this](const std::string& t) { subscribed.push_back(t); },
            [this](const std::string& t) { unsubscribed.push_back(t); },
            [this](script::MqttMessage m) { delivered.push_back(std::move(m)); });
  }
};

void offerText(ScriptMqttBridge& b, const char* topic, const std::string& body) {
  b.offer(topic, reinterpret_cast<const uint8_t*>(body.data()),
          static_cast<unsigned>(body.size()));
}

void test_publish_forwards_to_transport() {
  Transport tr;
  ScriptMqttBridge b;
  tr.wire(b);
  b.publish("home/light", "on");
  TEST_ASSERT_EQUAL_size_t(1, tr.published.size());
  TEST_ASSERT_EQUAL_STRING("home/light", tr.published[0].first.c_str());
  TEST_ASSERT_EQUAL_STRING("on", tr.published[0].second.c_str());
}

void test_empty_topic_is_ignored() {
  Transport tr;
  ScriptMqttBridge b;
  tr.wire(b);
  b.publish("", "x");
  b.subscribe("");
  TEST_ASSERT_EQUAL_size_t(0, tr.published.size());
  TEST_ASSERT_EQUAL_size_t(0, tr.subscribed.size());
  TEST_ASSERT_EQUAL_size_t(0, b.subscriptionCount());
}

void test_no_transport_is_a_silent_noop() {
  ScriptMqttBridge b;
  b.publish("a/b", "x");
  b.subscribe("a/b");
  b.unsubscribeAll("a/b");
  b.onReconnect();
  TEST_ASSERT_FALSE(b.offer("a/b", reinterpret_cast<const uint8_t*>("hi"), 2));
}

void test_subscribe_dedupes() {
  Transport tr;
  ScriptMqttBridge b;
  tr.wire(b);
  b.subscribe("sensor/#");
  b.subscribe("sensor/#");
  b.subscribe("other");
  TEST_ASSERT_EQUAL_size_t(2, tr.subscribed.size());
  TEST_ASSERT_EQUAL_size_t(2, b.subscriptionCount());
}

void test_unsubscribe_stops_delivery_and_tells_the_broker() {
  Transport tr;
  ScriptMqttBridge b;
  tr.wire(b);
  b.subscribe("sensor/temp");
  b.unsubscribeAll("sensor/temp");
  TEST_ASSERT_EQUAL_size_t(1, tr.unsubscribed.size());
  TEST_ASSERT_EQUAL_STRING("sensor/temp", tr.unsubscribed[0].c_str());
  TEST_ASSERT_EQUAL_size_t(0, b.subscriptionCount());
  offerText(b, "sensor/temp", "21");
  TEST_ASSERT_EQUAL_size_t(0, tr.delivered.size());
}

void test_unsubscribe_unknown_topic_is_harmless() {
  Transport tr;
  ScriptMqttBridge b;
  tr.wire(b);
  b.unsubscribeAll("never/subscribed");
  TEST_ASSERT_EQUAL_size_t(0, tr.unsubscribed.size());
}

void test_exact_delivery_carries_the_payload() {
  Transport tr;
  ScriptMqttBridge b;
  tr.wire(b);
  b.subscribe("sensor/temp");
  TEST_ASSERT_TRUE(b.offer("sensor/temp", reinterpret_cast<const uint8_t*>("21.5"), 4));
  TEST_ASSERT_EQUAL_size_t(1, tr.delivered.size());
  TEST_ASSERT_EQUAL_STRING("sensor/temp", tr.delivered[0].topic.c_str());
  TEST_ASSERT_EQUAL_STRING("21.5", tr.delivered[0].payload.c_str());
}

void test_wildcard_delivery_carries_filter_and_concrete_topic() {
  Transport tr;
  ScriptMqttBridge b;
  tr.wire(b);
  b.subscribe("sensor/#");
  offerText(b, "sensor/kitchen/temp", "21");
  TEST_ASSERT_EQUAL_size_t(1, tr.delivered.size());
  TEST_ASSERT_EQUAL_STRING("sensor/#", tr.delivered[0].filter.c_str());
  TEST_ASSERT_EQUAL_STRING("sensor/kitchen/temp", tr.delivered[0].topic.c_str());
  TEST_ASSERT_EQUAL_STRING("21", tr.delivered[0].payload.c_str());
}

void test_overlapping_filters_each_get_a_message() {
  Transport tr;
  ScriptMqttBridge b;
  tr.wire(b);
  b.subscribe("sensor/#");
  b.subscribe("sensor/+/temp");
  b.subscribe("sensor/kitchen/temp");
  b.subscribe("other/#");
  offerText(b, "sensor/kitchen/temp", "21");
  TEST_ASSERT_EQUAL_size_t(3, tr.delivered.size());
  TEST_ASSERT_EQUAL_STRING("sensor/#", tr.delivered[0].filter.c_str());
  TEST_ASSERT_EQUAL_STRING("sensor/+/temp", tr.delivered[1].filter.c_str());
  TEST_ASSERT_EQUAL_STRING("sensor/kitchen/temp", tr.delivered[2].filter.c_str());
  for (const auto& d : tr.delivered)
    TEST_ASSERT_EQUAL_STRING("sensor/kitchen/temp", d.topic.c_str());
}

void test_non_matching_delivery_is_dropped() {
  Transport tr;
  ScriptMqttBridge b;
  tr.wire(b);
  b.subscribe("sensor/#");
  TEST_ASSERT_FALSE(b.offer("other/thing", reinterpret_cast<const uint8_t*>("x"), 1));
  TEST_ASSERT_EQUAL_size_t(0, tr.delivered.size());
}

void test_payload_uses_length_not_termination() {
  Transport tr;
  ScriptMqttBridge b;
  tr.wire(b);
  b.subscribe("t");
  const uint8_t raw[] = {'a', 'b', 'c', 'd'};
  b.offer("t", raw, 2);
  TEST_ASSERT_EQUAL_size_t(1, tr.delivered.size());
  TEST_ASSERT_EQUAL_size_t(2, tr.delivered[0].payload.size());
  TEST_ASSERT_EQUAL_STRING("ab", tr.delivered[0].payload.c_str());
}

void test_empty_and_null_payloads_survive() {
  Transport tr;
  ScriptMqttBridge b;
  tr.wire(b);
  b.subscribe("t");
  b.offer("t", nullptr, 7);
  TEST_ASSERT_EQUAL_size_t(1, tr.delivered.size());
  TEST_ASSERT_EQUAL_size_t(0, tr.delivered[0].payload.size());
}

void test_null_topic_is_ignored() {
  Transport tr;
  ScriptMqttBridge b;
  tr.wire(b);
  b.subscribe("#");
  TEST_ASSERT_FALSE(b.offer(nullptr, reinterpret_cast<const uint8_t*>("x"), 1));
  TEST_ASSERT_EQUAL_size_t(0, tr.delivered.size());
}

void test_reconnect_replays_every_subscription() {
  Transport tr;
  ScriptMqttBridge b;
  tr.wire(b);
  b.subscribe("a/#");
  b.subscribe("b/+");
  tr.subscribed.clear();
  b.onReconnect();
  TEST_ASSERT_EQUAL_size_t(2, tr.subscribed.size());
  TEST_ASSERT_EQUAL_STRING("a/#", tr.subscribed[0].c_str());
  TEST_ASSERT_EQUAL_STRING("b/+", tr.subscribed[1].c_str());
}

void test_reconnect_does_not_replay_dropped_subscriptions() {
  Transport tr;
  ScriptMqttBridge b;
  tr.wire(b);
  b.subscribe("a/#");
  b.subscribe("b/+");
  b.unsubscribeAll("a/#");
  tr.subscribed.clear();
  b.onReconnect();
  TEST_ASSERT_EQUAL_size_t(1, tr.subscribed.size());
  TEST_ASSERT_EQUAL_STRING("b/+", tr.subscribed[0].c_str());
}

void test_subscription_count_is_capped() {
  Transport tr;
  ScriptMqttBridge b;
  tr.wire(b);
  for (int i = 0; i < 64; ++i) b.subscribe("t/" + std::to_string(i));
  TEST_ASSERT_EQUAL_size_t(32, b.subscriptionCount());
  TEST_ASSERT_EQUAL_size_t(32, tr.subscribed.size());
  b.unsubscribeAll("t/0");
  b.subscribe("fresh");
  TEST_ASSERT_EQUAL_size_t(32, b.subscriptionCount());
  TEST_ASSERT_EQUAL_STRING("fresh", tr.subscribed.back().c_str());
}

}

void setUp() {}
void tearDown() {}

int main(int, char**) {
  UNITY_BEGIN();
  RUN_TEST(test_publish_forwards_to_transport);
  RUN_TEST(test_empty_topic_is_ignored);
  RUN_TEST(test_no_transport_is_a_silent_noop);
  RUN_TEST(test_subscribe_dedupes);
  RUN_TEST(test_unsubscribe_stops_delivery_and_tells_the_broker);
  RUN_TEST(test_unsubscribe_unknown_topic_is_harmless);
  RUN_TEST(test_exact_delivery_carries_the_payload);
  RUN_TEST(test_wildcard_delivery_carries_filter_and_concrete_topic);
  RUN_TEST(test_overlapping_filters_each_get_a_message);
  RUN_TEST(test_non_matching_delivery_is_dropped);
  RUN_TEST(test_payload_uses_length_not_termination);
  RUN_TEST(test_empty_and_null_payloads_survive);
  RUN_TEST(test_null_topic_is_ignored);
  RUN_TEST(test_reconnect_replays_every_subscription);
  RUN_TEST(test_reconnect_does_not_replay_dropped_subscriptions);
  RUN_TEST(test_subscription_count_is_capped);
  UNITY_END();
  return 0;
}
