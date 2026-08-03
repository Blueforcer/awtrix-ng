#include <unity.h>

#include <string>
#include <string_view>

#include "persistence/DeviceConfig.h"

#include "../../src/persistence/DeviceConfigJson.cpp"

using namespace awtrix;

void setUp() {}
void tearDown() {}

namespace {

std::string written(const DeviceConfig& c, bool withSecrets) {
  std::string out;
  api::JsonWriter w(out);
  w.beginObject();
  c.write(w, withSecrets);
  w.endObject();
  return out;
}

std::string dump(const DeviceConfig& c) { return written(c, true); }

DeviceConfig seeded() {
  DeviceConfig c;
  c.wifiSsid = "net";
  c.wifiPass = "hunter2";
  c.netStatic = true;
  c.ip = "192.168.1.50";
  c.wifiConnectTimeout = 30000;
  c.mqttPort = 8883;
  c.tempOffset = -3.5f;
  c.lowBatteryThreshold = 15;
  c.brightnessSmoothing = 5000;
  c.tempDecimals = 2;
  c.hostname = "awtrix";
  c.pinMatrix = 21;
  c.panelWidth = 8;
  c.panels = 4;
  c.panelStart = PanelStart::BottomRight;
  c.panelWiring = Wiring::Columns;
  c.panelSerpentine = false;
  return c;
}

DeviceConfig after(const char* json) {
  DeviceConfig c = seeded();
  c.applyRead(api::JsonReader(json));
  return c;
}

void changesNothing(const char* json) {
  TEST_ASSERT_EQUAL_STRING_MESSAGE(dump(seeded()).c_str(), dump(after(json)).c_str(), json);
}

}

static int members(const std::string& json) {
  int n = 0;
  api::JsonReader r{std::string_view(json)};
  TEST_ASSERT_TRUE(r.enterObject());
  while (r.nextMember()) {
    ++n;
    TEST_ASSERT_TRUE(r.skipValue());
  }
  return n;
}

static void test_the_reply_carries_every_field() {
  TEST_ASSERT_EQUAL_INT(64, members(written(seeded(), false)));
  TEST_ASSERT_EQUAL_INT(67, members(written(seeded(), true)));
}

static void test_secrets_are_omitted_unless_asked_for() {
  const std::string open = written(seeded(), false);
  TEST_ASSERT_TRUE(open.find("hunter2") == std::string::npos);
  TEST_ASSERT_TRUE(open.find("wifiPass") == std::string::npos);
  TEST_ASSERT_TRUE(written(seeded(), true).find("hunter2") != std::string::npos);
}

static void test_every_type_reads_its_value() {
  TEST_ASSERT_EQUAL_STRING("other", after(R"({"wifiSsid":"other"})").wifiSsid.c_str());
  TEST_ASSERT_FALSE(after(R"({"netStatic":false})").netStatic);
  TEST_ASSERT_EQUAL_INT(9000, after(R"({"wifiConnectTimeout":9000})").wifiConnectTimeout);
  TEST_ASSERT_EQUAL_UINT16(1883, after(R"({"mqttPort":1883})").mqttPort);
  TEST_ASSERT_EQUAL_FLOAT(-9.5f, after(R"({"tempOffset":-9.5})").tempOffset);
  TEST_ASSERT_EQUAL_FLOAT(-9.0f, after(R"({"tempOffset":-9})").tempOffset);
  TEST_ASSERT_EQUAL_UINT8(20, after(R"({"lowBatteryThreshold":20})").lowBatteryThreshold);
  TEST_ASSERT_EQUAL_UINT8(1, after(R"({"tempDecimals":1})").tempDecimals);
  TEST_ASSERT_EQUAL_INT(-1, after(R"({"pinMatrix":-1})").pinMatrix);

  const DeviceConfig s = after(R"({"scriptingEnabled":false,"scriptLimit":4,"scriptMaxBytes":2048})");
  TEST_ASSERT_FALSE(s.scriptingEnabled);
  TEST_ASSERT_EQUAL_INT(4, s.scriptLimit);
  TEST_ASSERT_EQUAL_INT(2048, s.scriptMaxBytes);
  changesNothing("{}");
}

static void test_a_wrong_type_coerces_the_way_it_always_has() {
  changesNothing(R"({"wifiSsid":42})");
  changesNothing(R"({"wifiSsid":null})");
  changesNothing(R"({"wifiSsid":["a"]})");
  TEST_ASSERT_TRUE(after(R"({"netStatic":"yes"})").netStatic);
  TEST_ASSERT_TRUE(after(R"({"netStatic":1})").netStatic);
  TEST_ASSERT_FALSE(after(R"({"netStatic":null})").netStatic);
  TEST_ASSERT_EQUAL_UINT16(8883, after(R"({"mqttPort":"8883"})").mqttPort);
  TEST_ASSERT_EQUAL_UINT16(1, after(R"({"mqttPort":true})").mqttPort);
  TEST_ASSERT_EQUAL_UINT16(1, after(R"({"mqttPort":1.9})").mqttPort);
  TEST_ASSERT_EQUAL_UINT16(0, after(R"({"mqttPort":70000})").mqttPort);
  TEST_ASSERT_EQUAL_UINT16(0, after(R"({"mqttPort":-1})").mqttPort);
  TEST_ASSERT_EQUAL_UINT8(0, after(R"({"lowBatteryThreshold":300})").lowBatteryThreshold);
  TEST_ASSERT_EQUAL_FLOAT(0.0f, after(R"({"tempOffset":"nonsense"})").tempOffset);
  TEST_ASSERT_EQUAL_FLOAT(0.0f, after(R"({"tempOffset":null})").tempOffset);
  TEST_ASSERT_EQUAL_INT(0, after(R"({"wifiConnectTimeout":"12abc"})").wifiConnectTimeout);
}

static void test_an_empty_secret_is_not_a_clear() {
  changesNothing(R"({"wifiPass":""})");
  TEST_ASSERT_EQUAL_STRING("new", after(R"({"wifiPass":"new"})").wifiPass.c_str());
  TEST_ASSERT_EQUAL_STRING("", after(R"({"hostname":""})").hostname.c_str());
}

static void test_unknown_keys_are_ignored() {
  changesNothing(R"({"nonesuch":1})");
  changesNothing(R"({"nonesuch":{"nested":[1,2,3]}})");
  TEST_ASSERT_EQUAL_STRING("kept",
                           after(R"({"nonesuch":{"nested":[1,2,3]},"hostname":"kept"})").hostname.c_str());
  TEST_ASSERT_EQUAL_STRING("kept",
                           after(R"({"hostname":"kept","nonesuch":[{"a":1}]})").hostname.c_str());
}

static void test_enum_fields_travel_by_name() {
  TEST_ASSERT_TRUE(written(DeviceConfig{}, false).find("\"panelStart\":\"topLeft\"") !=
                   std::string::npos);
  TEST_ASSERT_TRUE(written(DeviceConfig{}, false).find("\"panelWiring\":\"rows\"") !=
                   std::string::npos);
  TEST_ASSERT_EQUAL_INT(static_cast<int>(PanelStart::BottomLeft),
                        static_cast<int>(after(R"({"panelStart":"bottomLeft"})").panelStart));
  TEST_ASSERT_EQUAL_INT(static_cast<int>(PanelStart::TopRight),
                        static_cast<int>(after(R"({"panelStart":"TOPRIGHT"})").panelStart));
  TEST_ASSERT_EQUAL_INT(static_cast<int>(Wiring::Rows),
                        static_cast<int>(after(R"({"panelWiring":"rows"})").panelWiring));
  changesNothing(R"({"panelStart":"sideways"})");
  changesNothing(R"({"panelStart":2})");
  changesNothing(R"({"panelWiring":true})");
}

static void test_the_whole_table_round_trips() {
  const DeviceConfig src = seeded();
  DeviceConfig back;
  back.applyRead(api::JsonReader(written(src, true)));
  TEST_ASSERT_EQUAL_STRING(dump(src).c_str(), dump(back).c_str());
}

int main(int, char**) {
  UNITY_BEGIN();
  RUN_TEST(test_the_reply_carries_every_field);
  RUN_TEST(test_secrets_are_omitted_unless_asked_for);
  RUN_TEST(test_every_type_reads_its_value);
  RUN_TEST(test_a_wrong_type_coerces_the_way_it_always_has);
  RUN_TEST(test_an_empty_secret_is_not_a_clear);
  RUN_TEST(test_unknown_keys_are_ignored);
  RUN_TEST(test_enum_fields_travel_by_name);
  RUN_TEST(test_the_whole_table_round_trips);
  return UNITY_END();
}
