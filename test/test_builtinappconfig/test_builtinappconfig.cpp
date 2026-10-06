#include "../EngineFakes.h"
#include <unity.h>

#include <string>
#include <vector>

#include "core/CoreEngine.h"
#include "core/api/ApiRouter.h"
#include "core/api/JsonReader.h"
#include "core/api/StateJson.h"
#include "core/api/StateReadApi.h"
#include "core/render/Canvas.h"

using namespace awtrix;

namespace {
using Display = awtrix::test::NullDisplay;
using System = awtrix::test::NullSystem;
struct Scripts : IScriptService {
  std::string name, payload;
  int configWrites = 0;
  DispatchResult setScript(const std::string&, const std::string&, DispatchDetail&) override {
    return DispatchResult::Ok;
  }
  void removeScript(const std::string&) override {}
  DispatchResult setScriptConfig(const std::string& n, const std::string& p,
                                 DispatchDetail& detail) override {
    name = n;
    payload = p;
    ++configWrites;
    detail.message = "script setup failed";
    detail.hook = "setup";
    return DispatchResult::Ok;
  }
};
struct Rig {
  sound::AudioRouter audio;
  Display display;
  System system;
  CoreEngine engine;
  Canvas screen{32, 8};
  std::string capabilities = "{}", body;
  script::ConfigTextFn source, store;
  api::StoredScriptsFn stored;
  api::DeviceStateFn device;
  api::StateReadContext context{engine, screen, capabilities, nullptr, source, store, stored, device};
  int changed = 0;

  explicit Rig(std::vector<std::string> apps = defaultBuiltinNames())
      : engine(audio, display, system, std::move(apps)) {
    engine.setTemperatureAvailable(false);
    engine.setHumidityAvailable(false);
    engine.setBatteryAvailable(false);
    engine.state().subscribe([this](StateEvent event) {
      if (event == StateEvent::SettingsChanged) ++changed;
    });
  }
  api::StateReadResult read(const std::string& path) {
    return api::readState("GET", path, context, body);
  }
  api::HttpResult write(const std::string& path, const std::string& payload) {
    Command cmd;
    api::HttpResult immediate;
    const auto routed = api::routeHttp("PATCH", path, std::string(payload), cmd, immediate);
    if (routed == api::RouteOutcome::Respond) return immediate;
    TEST_ASSERT_EQUAL_INT(static_cast<int>(api::RouteOutcome::Routed), static_cast<int>(routed));
    const auto result = engine.execute(cmd);
    return api::httpResponse(cmd, result, engine.lastDetail());
  }
  api::StateReadResult get(const std::string& name) {
    return read("/api/v1/apps/builtin/" + name + "/config");
  }
  api::HttpResult patch(const std::string& name, const std::string& payload) {
    return write("/api/v1/apps/builtin/" + name + "/config", payload);
  }
  DispatchResult legacy(const std::string& payload) {
    Command cmd(CommandType::SetSettings);
    cmd.payload = payload;
    return engine.execute(cmd);
  }
};

api::JsonReader field(const std::string& body, const char* wanted) {
  auto fields = api::memberValue(api::JsonReader(body), "fields");
  if (fields.enterArray()) {
    while (fields.nextElement()) {
      auto entry = api::JsonReader(fields.valueText());
      std::string key;
      api::memberValue(entry, "key").appendString(key);
      if (key == wanted) return entry;
      if (!fields.skipValue()) break;
    }
  }
  return {};
}

std::vector<std::string> keys(const std::string& body) {
  std::vector<std::string> out;
  auto fields = api::memberValue(api::JsonReader(body), "fields");
  TEST_ASSERT_TRUE(fields.enterArray());
  while (fields.nextElement()) {
    std::string key;
    TEST_ASSERT_TRUE(api::memberValue(api::JsonReader(fields.valueText()), "key").appendString(key));
    out.push_back(key);
    fields.skipValue();
  }
  return out;
}

bool offered(const std::string& body, const char* wanted) {
  return field(body, wanted).type() != api::JsonReader::Type::Invalid;
}

std::string text(api::JsonReader entry, const char* member) {
  return std::string(api::memberValue(entry, member).valueText());
}

std::string message(const std::string& body) {
  std::string out;
  api::memberValue(api::memberValue(api::JsonReader(body), "error"), "message").appendString(out);
  return out;
}

void test_builtin_get_uses_current_settings_defaults_and_the_device_profile() {
  Rig r;
  r.engine.state().settings().timeColor = {0x123456, true};
  auto result = r.get("Time");
  TEST_ASSERT_EQUAL_INT(200, result.status);
  TEST_ASSERT_TRUE(api::isWellFormed(r.body));
  const auto timeKeys = keys(r.body);
  TEST_ASSERT_TRUE(offered(r.body, "timeMode"));
  TEST_ASSERT_TRUE(offered(r.body, "timeShowAmPm"));
  TEST_ASSERT_FALSE(offered(r.body, "clockFace"));
  TEST_ASSERT_FALSE(offered(r.body, "dateColor"));
  TEST_ASSERT_FALSE(offered(r.body, "calendarAnimation"));
  auto color = field(r.body, "timeColor");
  long long value = 0;
  TEST_ASSERT_TRUE(api::memberValue(color, "value").asLong(value));
  TEST_ASSERT_EQUAL_HEX32(0x123456, value);
  TEST_ASSERT_TRUE(api::memberValue(color, "default").isNull());
  bool nullable = false;
  TEST_ASSERT_TRUE(api::memberValue(color, "nullable").asBool(nullable));
  TEST_ASSERT_TRUE(nullable);
  TEST_ASSERT_EQUAL_STRING("[\"weekdayBar\",\"weekendDays\"]",
                          text(field(r.body, "weekdayBar.weekendDays"), "path").c_str());
  TEST_ASSERT_EQUAL_STRING("\"days\"", text(field(r.body, "weekdayBar.weekendDays"), "type").c_str());
  TEST_ASSERT_EQUAL_STRING("[\"sunday\",\"saturday\"]",
                          text(field(r.body, "weekdayBar.weekendDays"), "default").c_str());
  TEST_ASSERT_EQUAL_STRING("\"select\"", text(field(r.body, "timeMode"), "type").c_str());
  TEST_ASSERT_EQUAL_STRING("[0,1,2,3,4,5,6]", text(field(r.body, "timeMode"), "options").c_str());
  TEST_ASSERT_EQUAL_STRING("[\"steady\",\"blink\",\"pulse\"]",
                          text(field(r.body, "timeSeparatorMode"), "options").c_str());
  TEST_ASSERT_EQUAL_STRING("\"pulse\"", text(field(r.body, "timeSeparatorMode"), "value").c_str());
  TEST_ASSERT_EQUAL_STRING("[\"time24h\"]", text(field(r.body, "time24h"), "path").c_str());
  TEST_ASSERT_FALSE(api::present(api::memberValue(field(r.body, "calendarTextColor"), "nullable")));

  r.engine.setClockFaces(true);
  r.get("Time");
  const auto facesKeys = keys(r.body);
  for (const char* key : {"clockFace", "calendarAnimation", "dateOrder", "dateSeparator",
                         "dateYearMode", "dateMonthNames", "dateColor"})
    TEST_ASSERT_TRUE_MESSAGE(offered(r.body, key), key);
  for (const char* key : {"timeMode", "timeShowAmPm", "dateShowWeekday"})
    TEST_ASSERT_FALSE_MESSAGE(offered(r.body, key), key);
  TEST_ASSERT_EQUAL_STRING("[\"sheet\",\"ring\",\"flap\",\"month\",\"big\"]",
                          text(field(r.body, "clockFace"), "options").c_str());
  r.get("Date");
  TEST_ASSERT_TRUE(offered(r.body, "dateShowWeekday"));
  TEST_ASSERT_FALSE(offered(r.body, "calendarHeaderColor"));
}

void test_app_patch_is_scoped_atomic_and_needs_no_script_interpreter() {
  Rig r;
  auto result = r.patch("Time", R"({"time24h":false,"weekdayBar":{"show":false,"activeColor":"#123456"}})");
  TEST_ASSERT_EQUAL_INT(200, result.status);
  TEST_ASSERT_NOT_EQUAL(std::string::npos, result.body.find("\"error\":null"));
  TEST_ASSERT_FALSE(r.engine.state().settings().time24h);
  TEST_ASSERT_FALSE(r.engine.state().settings().weekdayBar.show);
  TEST_ASSERT_TRUE(r.engine.state().settings().dateWeekdayBar.show);
  TEST_ASSERT_EQUAL_HEX32(0x123456, r.engine.state().settings().weekdayBar.activeColor);
  TEST_ASSERT_EQUAL_INT(1, r.changed);
  result = r.patch("Time", R"({"time24h":true,"brightness":12})");
  TEST_ASSERT_EQUAL_INT(422, result.status);
  TEST_ASSERT_FALSE(message(result.body).empty());
  TEST_ASSERT_EQUAL_STRING("brightness", r.engine.lastDetail().field.c_str());
  TEST_ASSERT_FALSE(r.engine.state().settings().time24h);
  TEST_ASSERT_EQUAL_INT(120, r.engine.state().settings().brightness);
  TEST_ASSERT_EQUAL_INT(1, r.changed);
  result = r.patch("Date", R"({"dateMonthNames":true,"weekdayBar":{"show":true,"activeColor":0}})");
  TEST_ASSERT_EQUAL_INT(200, result.status);
  TEST_ASSERT_FALSE(r.engine.state().settings().weekdayBar.show);
  TEST_ASSERT_EQUAL_HEX32(0x123456, r.engine.state().settings().weekdayBar.activeColor);
  TEST_ASSERT_EQUAL_HEX32(0, r.engine.state().settings().dateWeekdayBar.activeColor);
  result = r.patch("Date", R"({"dateMonthNames":false,"weekdayBar":{"show":"yes"}})");
  TEST_ASSERT_EQUAL_INT(422, result.status);
  TEST_ASSERT_EQUAL_STRING("weekdayBar.show", r.engine.lastDetail().field.c_str());
  TEST_ASSERT_TRUE(r.engine.state().settings().dateMonthNames);
  TEST_ASSERT_EQUAL_INT(2, r.changed);
  TEST_ASSERT_EQUAL_INT(422, r.patch("Date", "[]").status);
  TEST_ASSERT_EQUAL_INT(400, r.patch("Date", "{").status);
  TEST_ASSERT_EQUAL_INT(200, r.patch("Date", "{}").status);
  TEST_ASSERT_EQUAL_INT(2, r.changed);
}

void test_optional_colors_days_and_profile_filtered_writes_keep_their_types() {
  Rig r;
  TEST_ASSERT_EQUAL_INT(200, r.patch("Time", R"({"timeColor":0,"weekdayBar":{"weekendDays":["friday","saturday"]}})").status);
  TEST_ASSERT_TRUE(r.engine.state().settings().timeColor.set);
  TEST_ASSERT_EQUAL_HEX32(0, r.engine.state().settings().timeColor.rgb);
  TEST_ASSERT_EQUAL_UINT((1u << 5) | (1u << 6), r.engine.state().settings().weekdayBar.weekendMask);
  TEST_ASSERT_EQUAL_INT(200, r.patch("Time", R"({"timeColor":null})").status);
  TEST_ASSERT_FALSE(r.engine.state().settings().timeColor.set);
  TEST_ASSERT_EQUAL_INT(422, r.patch("Time", R"({"clockFace":"big"})").status);
  TEST_ASSERT_EQUAL_INT(422, r.patch("Time", R"({"timeMode":7})").status);
  TEST_ASSERT_EQUAL_INT(422, r.patch("Time", R"({"dateOrder":"yearMonthDay"})").status);
  r.engine.setClockFaces(true);
  TEST_ASSERT_EQUAL_INT(200, r.patch("Time", R"({"clockFace":"big","dateOrder":"yearMonthDay","dateColor":0})").status);
  TEST_ASSERT_EQUAL_INT(kClockFaceBig, r.engine.state().settings().clockFace);
  TEST_ASSERT_EQUAL_INT(kDateOrderYMD, r.engine.state().settings().dateOrder);
  TEST_ASSERT_EQUAL_INT(422, r.patch("Time", R"({"timeMode":1})").status);
  TEST_ASSERT_EQUAL_INT(422, r.patch("Time", R"({"weekdayBar":{"weekendDays":["notaday"]}})").status);
}

void test_only_builtins_the_device_draws_are_configurable() {
  Rig r;
  auto missing = r.get("Temperature");
  TEST_ASSERT_EQUAL_INT(404, missing.status);
  TEST_ASSERT_FALSE(message(r.body).empty());
  auto refused = r.patch("Temperature", R"({"useCelsius":false})");
  TEST_ASSERT_EQUAL_INT(404, refused.status);
  TEST_ASSERT_FALSE(message(refused.body).empty());
  r.engine.setTemperatureAvailable(true);
  TEST_ASSERT_EQUAL_INT(200, r.get("Temperature").status);
  TEST_ASSERT_EQUAL_UINT(2, keys(r.body).size());
  TEST_ASSERT_FALSE(api::present(api::memberValue(field(r.body, "useCelsius"), "group")));
  TEST_ASSERT_EQUAL_STRING("true", text(field(r.body, "temperatureColor"), "nullable").c_str());
  TEST_ASSERT_EQUAL_INT(200, r.patch("Temperature", R"({"useCelsius":false})").status);
  TEST_ASSERT_FALSE(r.engine.state().settings().useCelsius);
  TEST_ASSERT_TRUE(r.engine.setAppOrder(R"({"order":["Time"],"disabled":["Date"]})"));
  TEST_ASSERT_FALSE(r.engine.isEnabled("Date"));
  TEST_ASSERT_EQUAL_INT(200, r.get("Date").status);
  TEST_ASSERT_EQUAL_INT(200, r.patch("Date", R"({"dateShowWeekday":true})").status);
  TEST_ASSERT_EQUAL_INT(400, r.get("bad.name").status);
  TEST_ASSERT_EQUAL_INT(400, r.patch("bad.name", "{}").status);
  TEST_ASSERT_EQUAL_INT(404, r.get("Weather").status);
  TEST_ASSERT_EQUAL_INT(404, r.patch("Weather", "{}").status);

  DispatchDetail detail;
  TEST_ASSERT_EQUAL_INT(static_cast<int>(DispatchResult::Ok),
                       static_cast<int>(r.engine.setPushedApp("Time", R"({"text":"Shadow"})", detail)));
  TEST_ASSERT_EQUAL_INT(404, r.get("Time").status);
  TEST_ASSERT_EQUAL_INT(404, r.patch("Time", R"({"time24h":false})").status);
  TEST_ASSERT_TRUE(r.engine.state().settings().time24h);
}

void test_inventory_and_empty_builtin_config_match_available_controls() {
  Rig r({"Time", "Extra"});
  api::readState("GET", "/api/v1/apps", r.context, r.body);
  TEST_ASSERT_NOT_EQUAL(std::string::npos, r.body.find("\"origin\":\"builtin\",\"config\":true"));
  TEST_ASSERT_EQUAL(std::string::npos, r.body.find("Temperature"));
  TEST_ASSERT_EQUAL_INT(200, r.get("Extra").status);
  TEST_ASSERT_TRUE(keys(r.body).empty());
  const auto refused = r.patch("Extra", "{}");
  TEST_ASSERT_EQUAL_INT(422, refused.status);
  TEST_ASSERT_FALSE(message(refused.body).empty());
  TEST_ASSERT_EQUAL_INT(404, r.get("Date").status);
  api::readState("GET", "/api/v1/apps", r.context, r.body);
  TEST_ASSERT_NOT_EQUAL(std::string::npos, r.body.find("\"origin\":\"builtin\",\"config\":false"));
}

void test_the_app_config_route_belongs_to_scripts_whatever_else_has_the_name() {
  Rig r;
  Scripts scripts;
  r.engine.setScriptService(&scripts);
  int reads = 0;
  r.source = [&](const std::string&, std::string& out) {
    ++reads;
    out = "# @config city text default=Berlin\n";
    return true;
  };
  DispatchDetail detail;
  TEST_ASSERT_EQUAL_INT(static_cast<int>(DispatchResult::Ok),
                       static_cast<int>(r.engine.setPushedApp("Weather", R"({"text":"Pushed"})", detail)));
  for (const char* name : {"Weather", "Time"}) {
    const std::string path = std::string("/api/v1/apps/") + name + "/config";
    TEST_ASSERT_EQUAL_INT(200, r.read(path).status);
    TEST_ASSERT_TRUE(offered(r.body, "city"));
    const auto result = r.write(path, R"({"city":"Rome"})");
    TEST_ASSERT_EQUAL_INT(200, result.status);
    TEST_ASSERT_EQUAL_STRING(name, scripts.name.c_str());
    TEST_ASSERT_EQUAL_STRING("{\"city\":\"Rome\"}", scripts.payload.c_str());
    TEST_ASSERT_NOT_EQUAL(std::string::npos, result.body.find("script setup failed"));
    TEST_ASSERT_NOT_EQUAL(std::string::npos, result.body.find("\"hook\":\"setup\""));
  }
  TEST_ASSERT_EQUAL_INT(2, reads);
  TEST_ASSERT_EQUAL_INT(2, scripts.configWrites);
  TEST_ASSERT_EQUAL_INT(0, r.changed);
  TEST_ASSERT_TRUE(r.engine.state().settings().time24h);
  TEST_ASSERT_EQUAL_INT(200, r.patch("Time", R"({"time24h":false})").status);
  TEST_ASSERT_EQUAL_INT(2, scripts.configWrites);
  TEST_ASSERT_FALSE(r.engine.state().settings().time24h);
}

void test_legacy_bar_partial_broadcast_preserves_independent_unspecified_values() {
  Rig r;
  auto& s = r.engine.state().settings();
  s.weekdayBar.activeColor = 0x112233;
  s.dateWeekdayBar.activeColor = 0x445566;
  TEST_ASSERT_EQUAL_INT(static_cast<int>(DispatchResult::Ok),
                       static_cast<int>(r.legacy(R"({"weekdayBar":{"show":false}})")));
  TEST_ASSERT_FALSE(s.weekdayBar.show);
  TEST_ASSERT_FALSE(s.dateWeekdayBar.show);
  TEST_ASSERT_EQUAL_HEX32(0x112233, s.weekdayBar.activeColor);
  TEST_ASSERT_EQUAL_HEX32(0x445566, s.dateWeekdayBar.activeColor);
  TEST_ASSERT_EQUAL_INT(1, r.changed);
  for (const char* json : {R"({"weekdayBar":{"show":true},"dateWeekdayBar":{"show":false}})",
                           R"({"dateWeekdayBar":{"show":false},"weekdayBar":{"show":true}})"}) {
    TEST_ASSERT_EQUAL_INT(static_cast<int>(DispatchResult::Ok), static_cast<int>(r.legacy(json)));
    TEST_ASSERT_TRUE(s.weekdayBar.show);
    TEST_ASSERT_FALSE(s.dateWeekdayBar.show);
  }
  const std::string all = buildSettingsJson(r.engine);
  TEST_ASSERT_EQUAL_INT(static_cast<int>(DispatchResult::Ok), static_cast<int>(r.legacy(all)));
  TEST_ASSERT_TRUE(s.weekdayBar.show);
  TEST_ASSERT_FALSE(s.dateWeekdayBar.show);
  TEST_ASSERT_EQUAL_HEX32(0x112233, s.weekdayBar.activeColor);
  TEST_ASSERT_EQUAL_HEX32(0x445566, s.dateWeekdayBar.activeColor);
  TEST_ASSERT_EQUAL_INT(4, r.changed);
  for (const char* json : {
           R"({"weekdayBar":{"show":false,"inactiveColor":"#123456"},"dateWeekdayBar":{"activeColor":"#ABCDEF"}})",
           R"({"dateWeekdayBar":{"activeColor":"#ABCDEF"},"weekdayBar":{"show":false,"inactiveColor":"#123456"}})"}) {
    s.weekdayBar.show = true;
    s.dateWeekdayBar.show = true;
    TEST_ASSERT_EQUAL_INT(static_cast<int>(DispatchResult::Ok), static_cast<int>(r.legacy(json)));
    TEST_ASSERT_FALSE(s.weekdayBar.show);
    TEST_ASSERT_FALSE(s.dateWeekdayBar.show);
    TEST_ASSERT_EQUAL_HEX32(0x112233, s.weekdayBar.activeColor);
    TEST_ASSERT_EQUAL_HEX32(0xABCDEF, s.dateWeekdayBar.activeColor);
    TEST_ASSERT_EQUAL_HEX32(0x123456, s.weekdayBar.inactiveColor);
    TEST_ASSERT_EQUAL_HEX32(0x123456, s.dateWeekdayBar.inactiveColor);
  }
  TEST_ASSERT_EQUAL_INT(static_cast<int>(DispatchResult::ValidationError),
                       static_cast<int>(r.legacy(R"({"weekdayBar":{"show":true},"dateWeekdayBar":{"show":123}})")));
  TEST_ASSERT_FALSE(s.weekdayBar.show);
  TEST_ASSERT_EQUAL_INT(6, r.changed);
}

void test_repeated_legacy_members_apply_all_partial_patches_in_order() {
  Rig r;
  TEST_ASSERT_EQUAL_INT(static_cast<int>(DispatchResult::Ok), static_cast<int>(r.legacy(
      R"({"weekdayBar":{"show":false},"dateWeekdayBar":{"show":true},"weekdayBar":{"activeColor":123},"dateWeekdayBar":{"inactiveColor":456}})")));
  const auto& s = r.engine.state().settings();
  TEST_ASSERT_FALSE(s.weekdayBar.show);
  TEST_ASSERT_TRUE(s.dateWeekdayBar.show);
  TEST_ASSERT_EQUAL_UINT(123, s.weekdayBar.activeColor);
  TEST_ASSERT_EQUAL_UINT(123, s.dateWeekdayBar.activeColor);
  TEST_ASSERT_EQUAL_UINT(456, s.dateWeekdayBar.inactiveColor);
  TEST_ASSERT_EQUAL_INT(1, r.changed);
}
}

void setUp() {}
void tearDown() {}
int main() {
  UNITY_BEGIN();
  RUN_TEST(test_builtin_get_uses_current_settings_defaults_and_the_device_profile);
  RUN_TEST(test_app_patch_is_scoped_atomic_and_needs_no_script_interpreter);
  RUN_TEST(test_optional_colors_days_and_profile_filtered_writes_keep_their_types);
  RUN_TEST(test_only_builtins_the_device_draws_are_configurable);
  RUN_TEST(test_inventory_and_empty_builtin_config_match_available_controls);
  RUN_TEST(test_the_app_config_route_belongs_to_scripts_whatever_else_has_the_name);
  RUN_TEST(test_legacy_bar_partial_broadcast_preserves_independent_unspecified_values);
  RUN_TEST(test_repeated_legacy_members_apply_all_partial_patches_in_order);
  return UNITY_END();
}
