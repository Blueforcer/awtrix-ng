#include "../EngineFakes.h"
#include "core/sound/RoutedPcmSink.h"
#include <unity.h>

#include "AppConfig.h"
#include "core/CoreEngine.h"
#include "core/DeviceCapabilities.h"
#include "core/api/JsonReader.h"
#include "core/api/StateReadApi.h"
#include "core/render/Canvas.h"
#include "core/apps/AppRegistry.h"
#include "core/script/ScriptHost.h"

using namespace awtrix;

namespace {
using Display = awtrix::test::NullDisplay;
using System = awtrix::test::NullSystem;
// Plays every one-shot and holds it until told otherwise.
struct Pcm : sound::RoutedPcmSink {
  bool playing = false;
  void setVolumes(const sound::Volumes&) override {}
  bool playMp3(const std::string&, sound::Group) override { return playing = true; }
  void stopOneShot() override { playing = false; }
  bool oneShotPlaying() const override { return playing; }
  DispatchResult playStream(const std::string&, const std::string&, DispatchDetail&) override {
    return DispatchResult::Ok;
  }
  void stopStream() override {}
  void tick(int64_t) override {}
};
struct Assets : sound::IAssetProbe {
  bool hasFile(const std::string& path) const override { return path == "/MP3/ding.mp3"; }
};
struct Rig {
  sound::AudioRouter audio;
  Display display;
  System system;
  CoreEngine engine{audio, display, system};
  Canvas screen{52, 16};
  std::string capabilities = "{\"platform\":\"test\"}";
  script::ConfigTextFn source, store;
  api::StoredScriptsFn stored;
  api::DeviceStateFn device;
  api::StateReadContext context{engine, screen, capabilities, nullptr, source, store, stored, device};
  std::string body;

  api::StateReadResult get(const std::string& path) {
    return api::readState("GET", path, context, body);
  }
};

void test_device_state_comes_from_the_platform_and_defaults_to_an_empty_object() {
  Rig r;
  auto result = r.get("/api/v1/device");
  TEST_ASSERT_TRUE(result.matched);
  TEST_ASSERT_EQUAL_INT(200, result.status);
  TEST_ASSERT_EQUAL_STRING("application/json", result.contentType);
  TEST_ASSERT_EQUAL_STRING("{}", r.body.c_str());
  r.device = [](bool scripting) { return scripting ? "{\"scriptingRunning\":true}" :
                                                   "{\"scriptingRunning\":false}"; };
  r.get("/api/v1/device");
  TEST_ASSERT_EQUAL_STRING("{\"scriptingRunning\":false}", r.body.c_str());
}

void test_script_source_preserves_availability_validation_and_raw_text_contracts() {
  Rig r;
  auto result = r.get("/api/v1/apps/script/bad/name");
  TEST_ASSERT_TRUE(result.matched);
  TEST_ASSERT_EQUAL_INT(503, result.status);
  TEST_ASSERT_NOT_EQUAL(std::string::npos, r.body.find("unavailable"));
  int reads = 0;
  const std::string source = "# \"weather\"\nclass Weather\nend\n";
  r.source = [&](const std::string& name, std::string& out) {
    ++reads;
    if (name != "weather") return false;
    out = source;
    return true;
  };
  for (const auto& name : {"", "bad/name", "bad.name", "a2345678901234567890123456789012345"}) {
    result = r.get(std::string("/api/v1/apps/script/") + name);
    TEST_ASSERT_EQUAL_INT(400, result.status);
    TEST_ASSERT_NOT_EQUAL(std::string::npos, r.body.find("invalidName"));
  }
  TEST_ASSERT_EQUAL_INT(0, reads);
  result = r.get("/api/v1/apps/script/missing");
  TEST_ASSERT_EQUAL_INT(404, result.status);
  TEST_ASSERT_NOT_EQUAL(std::string::npos, r.body.find("notFound"));
  result = r.get("/api/v1/apps/script/weather");
  TEST_ASSERT_EQUAL_INT(200, result.status);
  TEST_ASSERT_EQUAL_STRING("text/plain", result.contentType);
  TEST_ASSERT_EQUAL_STRING(source.c_str(), r.body.c_str());
}

void test_script_configuration_combines_schema_defaults_with_persisted_values() {
  Rig r;
  auto result = r.get("/api/v1/apps/weather/config");
  TEST_ASSERT_TRUE(result.matched);
  TEST_ASSERT_EQUAL_INT(503, result.status);
  r.source = [](const std::string& name, std::string& out) {
    if (name != "weather") return false;
    out = "# @config city text \"City\" default=\"Berlin\" group=\"Weather data\"\n";
    return true;
  };
  r.store = [](const std::string&, std::string& out) {
    out = "{\"city\":\"London\"}";
    return true;
  };
  result = r.get("/api/v1/apps/weather/config");
  TEST_ASSERT_EQUAL_INT(200, result.status);
  TEST_ASSERT_TRUE(api::isWellFormed(r.body));
  TEST_ASSERT_NOT_EQUAL(std::string::npos, r.body.find("London"));
  TEST_ASSERT_NOT_EQUAL(std::string::npos, r.body.find("\"group\":\"Weather data\""));
  result = r.get("/api/v1/apps/missing/config");
  TEST_ASSERT_EQUAL_INT(404, result.status);
  result = r.get("/api/v1/apps/bad.name/config");
  TEST_ASSERT_EQUAL_INT(400, result.status);
  result = r.get("/api/v1/apps/script/weather/config");
  TEST_ASSERT_EQUAL_INT(400, result.status);
}

void test_script_data_leaves_the_settings_to_their_own_route() {
  Rig r;
  auto result = r.get("/api/v1/apps/game/data");
  TEST_ASSERT_TRUE(result.matched);
  TEST_ASSERT_EQUAL_INT(503, result.status);
  r.source = [](const std::string& name, std::string& out) {
    if (name != "game") return false;
    out = "# @config speed number default=3\n";
    return true;
  };
  r.store = [](const std::string&, std::string& out) {
    out = "{\"speed\":5,\"unl\":2}";
    return true;
  };
  result = r.get("/api/v1/apps/game/data");
  TEST_ASSERT_EQUAL_INT(200, result.status);
  TEST_ASSERT_EQUAL_STRING("application/json", result.contentType);
  TEST_ASSERT_EQUAL_STRING("{\"unl\":2}", r.body.c_str());
  TEST_ASSERT_EQUAL_INT(404, r.get("/api/v1/apps/missing/data").status);
  TEST_ASSERT_EQUAL_INT(400, r.get("/api/v1/apps/bad.name/data").status);
}

void test_state_reads_preserve_json_shapes_and_native_screen_coordinates() {
  Rig r;
  const char* paths[] = {"/api/v1/settings", "/api/v1/display", "/api/v1/display/screen",
                         "/api/v1/capabilities", "/api/v1/version", "/api/v1/audio"};
  for (const char* path : paths) {
    r.body = "previous response";
    const auto result = r.get(path);
    TEST_ASSERT_TRUE_MESSAGE(result.matched, path);
    TEST_ASSERT_EQUAL_INT(200, result.status);
    TEST_ASSERT_EQUAL_STRING("application/json", result.contentType);
    TEST_ASSERT_TRUE_MESSAGE(api::isWellFormed(r.body), path);
    TEST_ASSERT_TRUE_MESSAGE(api::JsonReader(r.body).isObject(), path);
  }
  r.screen.setPixel(51, 15, 0x123456);
  r.get("/api/v1/display/screen");
  long long width = 0, height = 0, pixel = 0;
  TEST_ASSERT_TRUE(api::memberValue(api::JsonReader(r.body), "width").asLong(width));
  TEST_ASSERT_TRUE(api::memberValue(api::JsonReader(r.body), "height").asLong(height));
  TEST_ASSERT_EQUAL_INT(52, width);
  TEST_ASSERT_EQUAL_INT(16, height);
  auto pixels = api::memberValue(api::JsonReader(r.body), "pixels");
  TEST_ASSERT_TRUE(pixels.enterArray());
  int count = 0;
  while (pixels.nextElement()) {
    TEST_ASSERT_TRUE(pixels.asLong(pixel));
    ++count;
    pixels.skipValue();
  }
  TEST_ASSERT_EQUAL_INT(52 * 16, count);
  TEST_ASSERT_EQUAL_HEX32(0x123456, pixel);
  r.get("/api/v1/capabilities");
  TEST_ASSERT_EQUAL_STRING(r.capabilities.c_str(), r.body.c_str());
  auto result = r.get("/version");
  TEST_ASSERT_TRUE(result.matched);
  TEST_ASSERT_EQUAL_STRING("text/plain", result.contentType);
  TEST_ASSERT_EQUAL_STRING(AWTRIX_NG_VERSION, r.body.c_str());
}

void test_app_inventory_uses_storage_only_when_the_script_host_is_unavailable() {
  Rig r;
  r.stored = [] {
    script::StoredScript stored;
    stored.name = "offline-weather";
    stored.meta.name = "Weather";
    return std::vector<script::StoredScript>{stored};
  };
  auto result = r.get("/api/v1/apps");
  TEST_ASSERT_TRUE(result.matched);
  TEST_ASSERT_EQUAL_INT(200, result.status);
  TEST_ASSERT_TRUE(api::isWellFormed(r.body));
  TEST_ASSERT_NOT_EQUAL(std::string::npos, r.body.find("offline-weather"));
  AppRegistry apps;
  script::ScriptServices services;
  script::ScriptHost scripts(apps, services, {}, {});
  r.context.scripts = &scripts;
  r.get("/api/v1/apps");
  TEST_ASSERT_EQUAL(std::string::npos, r.body.find("offline-weather"));
  TEST_ASSERT_NOT_EQUAL(std::string::npos, r.body.find("Time"));
}

void test_app_inventory_reports_whether_each_script_fits_this_device() {
  Rig r;
  r.stored = [] {
    script::StoredScript racer;
    racer.name = "racer";
    racer.meta = script::parseMeta("# @name Racer\n# @needs gamepad, audio.rtttl\n# @display 52x16\nimport gamepad\n");
    return std::vector<script::StoredScript>{racer};
  };

  r.get("/api/v1/apps");
  TEST_ASSERT_TRUE(api::isWellFormed(r.body));
  TEST_ASSERT_NOT_EQUAL(std::string::npos, r.body.find(R"("needs":[{"name":"gamepad"},{"name":"audio.rtttl"}])"));
  TEST_ASSERT_NOT_EQUAL(std::string::npos, r.body.find(R"("display":{"width":52,"height":16})"));

  sound::Caps buzzer;
  buzzer.rtttl = true;
  PlatformDescriptor tc001{"esp32", {32, 8, true}};
  const DeviceCapabilities device = DeviceCapabilities::from(buzzer, &tc001);
  r.context.device = &device;
  r.get("/api/v1/apps");
  TEST_ASSERT_TRUE(api::isWellFormed(r.body));
  TEST_ASSERT_NOT_EQUAL(std::string::npos,
      r.body.find(R"("needs":[{"name":"gamepad","missing":true},{"name":"audio.rtttl","missing":false}])"));
  TEST_ASSERT_NOT_EQUAL(std::string::npos, r.body.find(R"("display":{"width":52,"height":16,"fits":false})"));
}

void test_shared_state_requires_a_live_interpreter() {
  Rig r;
  auto result = r.get("/api/v1/scripts/shared");
  TEST_ASSERT_TRUE(result.matched);
  TEST_ASSERT_EQUAL_INT(503, result.status);
  AppRegistry apps;
  script::ScriptServices services;
  script::ScriptHost scripts(apps, services, {}, {});
  r.context.scripts = &scripts;
  result = r.get("/api/v1/scripts/shared");
  TEST_ASSERT_EQUAL_INT(200, result.status);
  TEST_ASSERT_EQUAL_STRING("[]", r.body.c_str());
  r.device = [](bool scripting) { return scripting ? "{\"live\":true}" : "{}"; };
  r.get("/api/v1/device");
  TEST_ASSERT_EQUAL_STRING("{\"live\":true}", r.body.c_str());
}

void test_configured_stations_are_readable_without_a_radio_output() {
  Rig r;
  DispatchDetail detail;
  const std::string stations = "{\"stations\":[{\"name\":\"Station\",\"url\":\"https://example.org/radio\"}]}";
  TEST_ASSERT_EQUAL_INT(static_cast<int>(DispatchResult::Ok),
                        static_cast<int>(r.engine.setStations(stations, detail)));
  auto result = r.get("/api/v1/audio/stations");
  TEST_ASSERT_TRUE(result.matched);
  TEST_ASSERT_EQUAL_INT(200, result.status);
  TEST_ASSERT_EQUAL_STRING("application/json", result.contentType);
  TEST_ASSERT_EQUAL_STRING(stations.c_str(), r.body.c_str());
}

void test_audio_metadata_control_characters_round_trip_as_valid_json() {
  Rig r;
  const std::string text = std::string("Title\n\r\t\b\f\"\\") + '\0' + '\x1f' + " end";
  auto& state = r.engine.state().runtime();
  state.radioTitle = text;
  state.radioStation = text;
  state.radioError = text;
  r.get("/api/v1/audio");
  for (unsigned char byte : r.body) TEST_ASSERT_GREATER_OR_EQUAL_UINT8(0x20, byte);
  TEST_ASSERT_TRUE(api::isWellFormed(r.body));
  const auto radio = api::memberValue(api::JsonReader(r.body), "radio");
  for (const char* field : {"title", "station", "error"}) {
    std::string decoded;
    TEST_ASSERT_TRUE(api::memberValue(radio, field).appendString(decoded));
    TEST_ASSERT_TRUE(decoded == text);
  }
}

// One object per group: the radio with its health first, then what the app and the alerts play.
void test_audio_reports_each_group() {
  Rig r;
  r.get("/api/v1/audio");
  const std::string& body = r.body;
  const auto has = [&body](const char* part) { return body.find(part) != std::string::npos; };
  TEST_ASSERT_TRUE(
      body.rfind("{\"radio\":{\"playing\":false,\"station\":\"\",\"title\":\"\",\"error\":\"\"", 0) == 0);
  TEST_ASSERT_TRUE(has(",\"app\":{\"playing\":false,\"name\":\"\",\"error\":\"\"}"));
  TEST_ASSERT_TRUE(has(",\"alert\":{\"playing\":false,\"name\":\"\",\"error\":\"\"}"));
  TEST_ASSERT_FALSE(has("\"available\""));
  TEST_ASSERT_TRUE(api::isWellFormed(body));

  Pcm pcm;
  Assets assets;
  r.audio.setPcm(&pcm);
  r.audio.setAssets(&assets);
  sound::Choices choices;
  DispatchDetail detail;
  TEST_ASSERT_TRUE(sound::parse("\"ding\"", sound::Origin::Play, choices, detail));
  r.audio.play(choices, sound::Group::Alert, "", detail);
  r.get("/api/v1/audio");
  TEST_ASSERT_TRUE(has(",\"alert\":{\"playing\":true,\"name\":\"ding\",\"error\":\"\"}"));
}

void test_unmatched_requests_leave_the_body_untouched_and_repeated_reads_replace_it() {
  Rig r;
  r.body = "previous handler";
  const auto unknown = r.get("/api/v1/system");
  TEST_ASSERT_FALSE(unknown.matched);
  TEST_ASSERT_EQUAL_STRING("previous handler", r.body.c_str());
  const auto wrongMethod = api::readState("PUT", "/api/v1/settings", r.context, r.body);
  TEST_ASSERT_FALSE(wrongMethod.matched);
  TEST_ASSERT_EQUAL_STRING("previous handler", r.body.c_str());
  r.body.reserve(8192);
  const size_t capacity = r.body.capacity();
  for (const char* path : {"/api/v1/audio", "/api/v1/apps"}) {
    auto result = r.get(path);
    TEST_ASSERT_TRUE(result.reuseBuffer);
    const std::string first = r.body;
    r.get(path);
    TEST_ASSERT_EQUAL_STRING(first.c_str(), r.body.c_str());
    TEST_ASSERT_EQUAL_size_t(capacity, r.body.capacity());
  }
  TEST_ASSERT_FALSE(r.get("/api/v1/display/screen").reuseBuffer);
  r.source = [](const std::string&, std::string& out) { out = "source"; return true; };
  TEST_ASSERT_FALSE(r.get("/api/v1/apps/script/weather").reuseBuffer);
  // A script's sounds are listed by the transport, not read here as a malformed script name.
  r.body = "previous handler";
  for (const char* path : {"/api/v1/apps/script/weather/sounds",
                           "/api/v1/apps/script/weather/sounds/rain"})
    TEST_ASSERT_FALSE_MESSAGE(r.get(path).matched, path);
  TEST_ASSERT_EQUAL_STRING("previous handler", r.body.c_str());
}
}

void setUp() {}
void tearDown() {}

int main() {
  UNITY_BEGIN();
  RUN_TEST(test_device_state_comes_from_the_platform_and_defaults_to_an_empty_object);
  RUN_TEST(test_script_source_preserves_availability_validation_and_raw_text_contracts);
  RUN_TEST(test_script_configuration_combines_schema_defaults_with_persisted_values);
  RUN_TEST(test_script_data_leaves_the_settings_to_their_own_route);
  RUN_TEST(test_state_reads_preserve_json_shapes_and_native_screen_coordinates);
  RUN_TEST(test_app_inventory_uses_storage_only_when_the_script_host_is_unavailable);
  RUN_TEST(test_app_inventory_reports_whether_each_script_fits_this_device);
  RUN_TEST(test_shared_state_requires_a_live_interpreter);
  RUN_TEST(test_configured_stations_are_readable_without_a_radio_output);
  RUN_TEST(test_audio_metadata_control_characters_round_trip_as_valid_json);
  RUN_TEST(test_audio_reports_each_group);
  RUN_TEST(test_unmatched_requests_leave_the_body_untouched_and_repeated_reads_replace_it);
  return UNITY_END();
}
