#include <unity.h>

#include <algorithm>
#include <functional>
#include <string>
#include <vector>

#include "core/Transitions.h"
#include "core/api/ApiRouter.h"
#include "core/api/JsonReader.h"
#include "core/mqtt/ByteSink.h"
#include "core/mqtt/HaDiscovery.h"

using awtrix::ha::CountingSink;
using awtrix::ha::DiscoveryContext;
using awtrix::ha::emit;
using awtrix::ha::StringSink;

namespace {

DiscoveryContext baseContext() {
  DiscoveryContext ctx;
  ctx.prefix = "awtrix_b3c4d5";
  ctx.haPrefix = "homeassistant";
  ctx.uid = "a4cf12b3c4d5";
  ctx.hostname = "awtrix-livingroom";
  ctx.version = "1.2.3";
  return ctx;
}

std::string emitToString(const DiscoveryContext& ctx) {
  StringSink sink;
  emit(ctx, sink);
  return sink.str;
}

using awtrix::api::JsonReader;
using awtrix::api::memberValue;

JsonReader parsed(const std::string& json) {
  JsonReader probe{std::string_view(json)};
  TEST_ASSERT_TRUE_MESSAGE(probe.skipValue() && probe.atEnd(), "not JSON");
  return JsonReader{std::string_view(json)};
}

JsonReader at(JsonReader o, const char* key) { return memberValue(o, key); }
bool absent(JsonReader o, const char* key) {
  return memberValue(o, key).type() == JsonReader::Type::Invalid;
}
std::string str(JsonReader o, const char* key) {
  std::string v;
  memberValue(o, key).appendString(v);
  return v;
}
void forEachMember(JsonReader o, const std::function<void(const std::string&, JsonReader)>& fn) {
  if (!o.enterObject()) return;
  while (o.nextMember()) {
    fn(std::string(o.key()), o);
    if (!o.skipValue()) break;
  }
}
std::size_t memberCount(JsonReader o) {
  std::size_t n = 0;
  forEachMember(o, [&](const std::string&, JsonReader) { ++n; });
  return n;
}

void test_emits_device_block_from_context() {
  const DiscoveryContext ctx = baseContext();
  const std::string json = emitToString(ctx);

  const JsonReader doc = parsed(json);

  const JsonReader dev = at(doc, "dev");
  TEST_ASSERT_TRUE(dev.isObject());
  TEST_ASSERT_EQUAL_STRING("a4cf12b3c4d5", str(dev, "ids").c_str());
  TEST_ASSERT_EQUAL_STRING("awtrix-livingroom", str(dev, "name").c_str());
  TEST_ASSERT_EQUAL_STRING("1.2.3", str(dev, "sw").c_str());
}

void test_escapes_quotes_and_backslashes_in_hostname() {
  DiscoveryContext ctx = baseContext();
  ctx.hostname = "aw\"trix\\living";
  const std::string json = emitToString(ctx);

  const JsonReader doc = parsed(json);
  TEST_ASSERT_EQUAL_STRING("aw\"trix\\living", str(at(doc, "dev"), "name").c_str());
}

void test_declares_base_topic_availability_and_origin() {
  const DiscoveryContext ctx = baseContext();
  const std::string json = emitToString(ctx);

  const JsonReader doc = parsed(json);

  TEST_ASSERT_TRUE(absent(doc, "~"));
  TEST_ASSERT_EQUAL_STRING("awtrix_b3c4d5/availability", str(doc, "avty_t").c_str());
  TEST_ASSERT_EQUAL_STRING("online", str(doc, "pl_avail").c_str());
  TEST_ASSERT_EQUAL_STRING("offline", str(doc, "pl_not_avail").c_str());
  TEST_ASSERT_EQUAL_STRING("awtrix-ng", str(at(doc, "o"), "name").c_str());
  TEST_ASSERT_EQUAL_STRING("1.2.3", str(at(doc, "o"), "sw").c_str());
}

void test_discovery_topic_is_device_scoped() {
  DiscoveryContext ctx = baseContext();
  TEST_ASSERT_EQUAL_STRING("homeassistant/device/a4cf12b3c4d5/config",
                           awtrix::ha::discoveryTopic(ctx).c_str());
  ctx.haPrefix = "ha-discovery";
  TEST_ASSERT_EQUAL_STRING("ha-discovery/device/a4cf12b3c4d5/config",
                           awtrix::ha::discoveryTopic(ctx).c_str());
}

void test_counting_pass_length_matches_the_streaming_pass() {
  for (unsigned mask = 0; mask < 32; ++mask) {
    DiscoveryContext ctx = baseContext();
    ctx.hasBattery = (mask & 1) != 0;
    ctx.hasTemperature = (mask & 2) != 0;
    ctx.hasHumidity = (mask & 4) != 0;
    ctx.hasPressure = (mask & 8) != 0;
    ctx.hasLightSensor = (mask & 16) != 0;

    CountingSink counting;
    emit(ctx, counting);
    StringSink streaming;
    emit(ctx, streaming);

    TEST_ASSERT_EQUAL_size_t(streaming.str.size(), counting.n);
  }
}

void test_matrix_light_targets_the_existing_command_topics() {
  const std::string json = emitToString(baseContext());

  const JsonReader doc = parsed(json);

  const JsonReader m = at(at(doc, "cmps"), "mat");
  TEST_ASSERT_TRUE(m.isObject());
  TEST_ASSERT_EQUAL_STRING("light", str(m, "p").c_str());
  TEST_ASSERT_EQUAL_STRING("a4cf12b3c4d5_mat", str(m, "uniq_id").c_str());
  TEST_ASSERT_EQUAL_STRING("~/cmd/display", str(m, "cmd_t").c_str());
  TEST_ASSERT_EQUAL_STRING("~/state/device", str(m, "stat_t").c_str());
  TEST_ASSERT_EQUAL_STRING("~/cmd/settings", str(m, "bri_cmd_t").c_str());
  TEST_ASSERT_EQUAL_STRING("~/state/settings", str(m, "rgb_stat_t").c_str());
}

struct Expected {
  const char* key;
  const char* platform;
};

const Expected kAlwaysPresent[] = {
    {"mat", "light"},          {"ind1", "light"},        {"ind2", "light"},
    {"ind3", "light"},         {"brimode", "select"},    {"transeff", "select"},
    {"trans", "switch"},       {"next", "button"},       {"prev", "button"},
    {"dismiss", "button"},     {"app", "sensor"},
    {"ver", "sensor"},         {"ip", "sensor"},         {"prefix", "sensor"},
    {"rssi", "sensor"},        {"uptime", "sensor"},     {"ram", "sensor"},
    {"btnl", "binary_sensor"}, {"btnm", "binary_sensor"}, {"btnr", "binary_sensor"},
};

void test_emits_the_full_entity_set_with_correct_platforms() {
  DiscoveryContext ctx = baseContext();
  ctx.hasBattery = true;
  ctx.hasTemperature = true;
  ctx.hasHumidity = true;
  ctx.hasPressure = true;
  ctx.hasLightSensor = true;
  const std::string json = emitToString(ctx);

  const JsonReader doc = parsed(json);
  const JsonReader cmps = at(doc, "cmps");
  TEST_ASSERT_TRUE(cmps.isObject());

  for (const Expected& e : kAlwaysPresent) {
    const JsonReader c = at(cmps, e.key);
    TEST_ASSERT_TRUE_MESSAGE(c.isObject(), e.key);
    TEST_ASSERT_EQUAL_STRING_MESSAGE(e.platform, str(c, "p").c_str(), e.key);
  }
  const Expected kGated[] = {{"temp", "sensor"},  {"hum", "sensor"}, {"press", "sensor"},
                             {"bat", "sensor"},   {"batv", "sensor"},
                             {"lowbat", "binary_sensor"}, {"light", "sensor"}};
  for (const Expected& e : kGated) {
    const JsonReader c = at(cmps, e.key);
    TEST_ASSERT_TRUE_MESSAGE(c.isObject(), e.key);
    TEST_ASSERT_EQUAL_STRING_MESSAGE(e.platform, str(c, "p").c_str(), e.key);
  }
  TEST_ASSERT_EQUAL_size_t(27, memberCount(cmps));
}

void test_absent_hardware_omits_its_entities() {
  const std::string json = emitToString(baseContext());

  const JsonReader doc = parsed(json);
  const JsonReader cmps = at(doc, "cmps");

  TEST_ASSERT_TRUE(absent(cmps, "temp"));
  TEST_ASSERT_TRUE(absent(cmps, "hum"));
  TEST_ASSERT_TRUE(absent(cmps, "press"));
  TEST_ASSERT_TRUE(absent(cmps, "bat"));
  TEST_ASSERT_TRUE(absent(cmps, "batv"));
  TEST_ASSERT_TRUE(absent(cmps, "lowbat"));
  TEST_ASSERT_TRUE(absent(cmps, "light"));
  TEST_ASSERT_EQUAL_size_t(20, memberCount(cmps));
}

void test_transition_options_follow_the_firmware_list() {
  const std::string json = emitToString(baseContext());

  const JsonReader doc = parsed(json);
  JsonReader ops = at(at(at(doc, "cmps"), "transeff"), "ops");
  TEST_ASSERT_TRUE(ops.enterArray());
  std::size_t i = 0;
  while (ops.nextElement()) {
    std::string name;
    ops.appendString(name);
    TEST_ASSERT_TRUE(i < awtrix::kTransitionCount);
    TEST_ASSERT_EQUAL_STRING(awtrix::kTransitionNames[i], name.c_str());
    ++i;
    TEST_ASSERT_TRUE(ops.skipValue());
  }
  TEST_ASSERT_EQUAL_size_t(awtrix::kTransitionCount, i);
}

void test_every_command_topic_is_routable() {
  DiscoveryContext ctx = baseContext();
  ctx.hasBattery = true;
  ctx.hasTemperature = true;
  ctx.hasHumidity = true;
  ctx.hasPressure = true;
  ctx.hasLightSensor = true;
  const std::string json = emitToString(ctx);

  const JsonReader doc = parsed(json);

  static const char* kCommandKeys[] = {"cmd_t", "bri_cmd_t", "rgb_cmd_t"};
  int checked = 0;
  forEachMember(at(doc, "cmps"), [&](const std::string& key, JsonReader comp) {
    for (const char* ck : kCommandKeys) {
      if (absent(comp, ck)) continue;
      std::string topic = str(comp, ck);
      TEST_ASSERT_EQUAL_STRING_MESSAGE("~/", topic.substr(0, 2).c_str(), key.c_str());
      const std::string suffix = topic.substr(2);

      awtrix::Command cmd;
      std::string result;
      const awtrix::api::RouteOutcome outcome = awtrix::api::routeMqtt(suffix, "{}", cmd, result);
      TEST_ASSERT_TRUE_MESSAGE(outcome != awtrix::api::RouteOutcome::NoMatch, suffix.c_str());
      ++checked;
    }
  });
  TEST_ASSERT_TRUE(checked >= 10);
}

void test_state_topics_are_ones_the_firmware_already_publishes() {
  DiscoveryContext ctx = baseContext();
  ctx.hasBattery = true;
  ctx.hasTemperature = true;
  ctx.hasHumidity = true;
  ctx.hasPressure = true;
  ctx.hasLightSensor = true;
  const std::string json = emitToString(ctx);

  const JsonReader doc = parsed(json);

  static const char* kKnown[] = {
      "~/state/device",        "~/state/settings",       "~/state/apps/active",
      "~/state/buttons/left",  "~/state/buttons/select", "~/state/buttons/right",
      "~/state/prefix",
  };
  static const char* kStateKeys[] = {"stat_t", "bri_stat_t", "rgb_stat_t"};
  forEachMember(at(doc, "cmps"), [&](const std::string&, JsonReader comp) {
    for (const char* sk : kStateKeys) {
      if (absent(comp, sk)) continue;
      const std::string topic = str(comp, sk);
      bool known = false;
      for (const char* k : kKnown) known = known || topic == k;
      TEST_ASSERT_TRUE_MESSAGE(known, topic.c_str());
    }
  });
}

void test_indicator_off_payload_is_a_command_the_firmware_accepts() {
  const std::string json = emitToString(baseContext());

  const JsonReader doc = parsed(json);

  for (const char* key : {"ind1", "ind2", "ind3"}) {
    const JsonReader c = at(at(doc, "cmps"), key);
    const std::string off = str(c, "pl_off");
    TEST_ASSERT_EQUAL_STRING_MESSAGE("{\"color\":[0,0,0]}", off.c_str(), key);
    const std::string tpl = str(c, "stat_val_tpl");
    TEST_ASSERT_TRUE_MESSAGE(tpl.find("{% else %}" + off + "{% endif %}") != std::string::npos,
                             key);
    TEST_ASSERT_EQUAL_STRING_MESSAGE("first", str(c, "on_cmd_type").c_str(), key);
  }
}

void test_every_component_carries_the_topic_base() {
  DiscoveryContext ctx = baseContext();
  ctx.hasBattery = true;
  ctx.hasTemperature = true;
  ctx.hasHumidity = true;
  ctx.hasPressure = true;
  ctx.hasLightSensor = true;
  const std::string json = emitToString(ctx);

  const JsonReader doc = parsed(json);

  forEachMember(at(doc, "cmps"), [](const std::string& key, JsonReader comp) {
    TEST_ASSERT_EQUAL_STRING_MESSAGE("awtrix_b3c4d5", str(comp, "~").c_str(), key.c_str());
  });
}

void test_unique_ids_are_distinct() {
  DiscoveryContext ctx = baseContext();
  ctx.hasBattery = true;
  ctx.hasTemperature = true;
  ctx.hasHumidity = true;
  ctx.hasPressure = true;
  ctx.hasLightSensor = true;
  const std::string json = emitToString(ctx);

  const JsonReader doc = parsed(json);

  std::vector<std::string> ids;
  forEachMember(at(doc, "cmps"), [&](const std::string&, JsonReader comp) {
    ids.push_back(str(comp, "uniq_id"));
  });

  TEST_ASSERT_EQUAL_size_t(27, ids.size());
  std::sort(ids.begin(), ids.end());
  TEST_ASSERT_TRUE(std::adjacent_find(ids.begin(), ids.end()) == ids.end());
}

}

void setUp() {}
void tearDown() {}

int main(int, char**) {
  UNITY_BEGIN();
  RUN_TEST(test_emits_device_block_from_context);
  RUN_TEST(test_escapes_quotes_and_backslashes_in_hostname);
  RUN_TEST(test_declares_base_topic_availability_and_origin);
  RUN_TEST(test_discovery_topic_is_device_scoped);
  RUN_TEST(test_counting_pass_length_matches_the_streaming_pass);
  RUN_TEST(test_matrix_light_targets_the_existing_command_topics);
  RUN_TEST(test_emits_the_full_entity_set_with_correct_platforms);
  RUN_TEST(test_absent_hardware_omits_its_entities);
  RUN_TEST(test_transition_options_follow_the_firmware_list);
  RUN_TEST(test_every_command_topic_is_routable);
  RUN_TEST(test_state_topics_are_ones_the_firmware_already_publishes);
  RUN_TEST(test_indicator_off_payload_is_a_command_the_firmware_accepts);
  RUN_TEST(test_every_component_carries_the_topic_base);
  RUN_TEST(test_unique_ids_are_distinct);
  UNITY_END();
  return 0;
}
