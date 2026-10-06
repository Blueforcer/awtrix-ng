#include <unity.h>

#include <algorithm>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>

#include "core/DeviceCapabilities.h"
#include "core/api/CapabilitiesJson.h"
#include "core/api/JsonReader.h"
#include "core/script/ScriptMeta.h"
#include "platform/linux/LinuxCapabilities.h"

using namespace awtrix;

void setUp() {}
void tearDown() {}

static PlatformDescriptor everything() {
  PlatformDescriptor platform{"test", {52, 16, false}};
  platform.lightSensor = true;
  platform.microphonePcm = true;
  return platform;
}

// The names @needs can use, as the header parsers learn them from the shared vectors.
static void test_the_table_names_exactly_the_shared_vocabulary() {
  std::ifstream in("test/fixtures/script_header_vectors.json", std::ios::binary);
  std::stringstream text;
  text << in.rdbuf();
  const std::string json = text.str();
  TEST_ASSERT_FALSE_MESSAGE(json.empty(), "test/fixtures/script_header_vectors.json is not readable");

  api::JsonReader list = api::memberValue(api::JsonReader(json), "vocabulary");
  TEST_ASSERT_TRUE(list.enterArray());
  std::vector<std::string> vocabulary;
  while (list.nextElement()) {
    std::string name;
    list.appendString(name);
    vocabulary.push_back(name);
    list.skipValue();
  }
  std::vector<std::string> table(DeviceCapabilities::kNames.begin(), DeviceCapabilities::kNames.end());
  const layout::Limits layoutLimits;
  const LinuxCapabilities platformCaps(true, true, &layoutLimits);
  for (const auto* item = platformCaps.needs(); item->name; ++item) table.emplace_back(item->name);
  std::sort(vocabulary.begin(), vocabulary.end());
  std::sort(table.begin(), table.end());
  TEST_ASSERT_TRUE(vocabulary == table);
}

// Every name in the table must be readable from GET /api/v1/capabilities at its dotted path,
// or a Hub and a web UI could never tell that the device has it.
static void test_capabilities_json_reports_every_table_entry() {
  sound::Caps audio{true, true, true, true, true, true, true, true, true};
  const PlatformDescriptor platform = everything();
  const layout::Limits layoutLimits;
  const LinuxCapabilities platformCaps(true, true, &layoutLimits);
  const std::string json = api::capabilitiesJson({}, {}, {}, audio, &platform, nullptr,
      [&](api::JsonWriter& writer) { platformCaps.write(writer); });
  TEST_ASSERT_TRUE(api::isWellFormed(json));
  TEST_ASSERT_TRUE(json.find("\"audio\":{\"mp3\":true,\"rtttl\":true,\"song\":true,\"speech\":true,"
                             "\"track\":true,\"radio\":true,\"url\":true,\"effect\":true,"
                             "\"clip\":true},\"microphone\":true") != std::string::npos);

  for (std::string_view name : DeviceCapabilities::kNames) {
    api::JsonReader value(json);
    std::string_view rest = name;
    while (true) {
      const std::size_t dot = rest.find('.');
      const std::string part(rest.substr(0, dot));
      value = api::memberValue(value, part.c_str());
      if (dot == std::string_view::npos) break;
      rest = rest.substr(dot + 1);
    }
    bool present = false;
    TEST_ASSERT_TRUE_MESSAGE(value.asBool(present), std::string(name).c_str());
    TEST_ASSERT_TRUE_MESSAGE(present, std::string(name).c_str());
  }
  const DeviceCapabilities caps = DeviceCapabilities::from(audio, &platform, platformCaps.needs());
  for (const auto* item = platformCaps.needs(); item->name; ++item) {
    bool present = false;
    TEST_ASSERT_TRUE(api::memberValue(api::JsonReader(json), item->name).asBool(present));
    TEST_ASSERT_TRUE(present);
    TEST_ASSERT_TRUE(caps.has(item->name));
  }
}

static void test_the_table_follows_the_hardware() {
  sound::Caps buzzerOnly;
  buzzerOnly.rtttl = true;
  PlatformDescriptor tc001{"esp32", {32, 8, true}};
  tc001.lightSensor = true;
  const DeviceCapabilities caps = DeviceCapabilities::from(buzzerOnly, &tc001);

  TEST_ASSERT_TRUE(caps.has("audio.rtttl"));
  TEST_ASSERT_TRUE(caps.has("sensors.light"));
  TEST_ASSERT_FALSE(caps.has("audio.mp3"));
  TEST_ASSERT_FALSE(caps.has("gamepad"));
  TEST_ASSERT_FALSE(caps.has("microphone"));
  TEST_ASSERT_FALSE(caps.has("audio.speech"));
  TEST_ASSERT_FALSE(caps.has("hologram"));
  TEST_ASSERT_TRUE(caps.fits(32, 8));
  TEST_ASSERT_TRUE(caps.fits(0, 0));
  TEST_ASSERT_FALSE(caps.fits(52, 16));

  PlatformDescriptor tc002{"tc002", {52, 16, false}};
  tc002.microphonePcm = true;
  TEST_ASSERT_TRUE(DeviceCapabilities::from(buzzerOnly, &tc002).has("microphone"));
  bool microphone = true;
  TEST_ASSERT_TRUE(api::memberValue(api::JsonReader(api::capabilitiesJson({}, {}, {}, buzzerOnly, &tc001)),
                                    "microphone").asBool(microphone));
  TEST_ASSERT_FALSE(microphone);

  const DeviceCapabilities none = DeviceCapabilities::from(buzzerOnly, nullptr);
  TEST_ASSERT_FALSE(none.has("sensors.light"));
  TEST_ASSERT_FALSE(none.has("microphone"));
  TEST_ASSERT_FALSE(none.fits(8, 8));
}

// url and clip are reported, but are not @needs names.
static void test_url_and_clip_are_reported_but_not_a_need() {
  sound::Caps audio;
  audio.url = audio.clip = true;
  const PlatformDescriptor platform = everything();
  const std::string json = api::capabilitiesJson({}, {}, {}, audio, &platform);
  for (const char* key : {"url", "clip"}) {
    bool present = false;
    TEST_ASSERT_TRUE_MESSAGE(api::memberValue(api::memberValue(api::JsonReader(json), "audio"), key)
                                 .asBool(present),
                             key);
    TEST_ASSERT_TRUE_MESSAGE(present, key);
  }
  for (std::string_view name : DeviceCapabilities::kNames)
    TEST_ASSERT_TRUE(name != "audio.url" && name != "audio.clip");
  TEST_ASSERT_TRUE(json.find("scriptSounds") == std::string::npos);
  TEST_ASSERT_TRUE(json.find("audioInputs") == std::string::npos);
}

static void test_linux_needs_remain_valid_but_missing_without_the_platform() {
  const layout::Limits layoutLimits;
  const LinuxCapabilities platformCaps(true, true, &layoutLimits);
  const DeviceCapabilities none = DeviceCapabilities::from({}, nullptr);
  const std::string json = api::capabilitiesJson({}, {}, {}, {});
  for (const auto* item = platformCaps.needs(); item->name; ++item) {
    const auto meta = script::parseMeta(std::string("# @needs ") + item->name + "\ndef draw() end\n");
    TEST_ASSERT_EQUAL_STRING(item->name, meta.needs.c_str());
    TEST_ASSERT_FALSE(none.has(item->name));
    TEST_ASSERT_FALSE(api::present(api::memberValue(api::JsonReader(json), item->name)));
  }
  for (const char* name : {"voice", "clockFaces", "mqttTls", "bootSound", "enlargeApps"})
    TEST_ASSERT_FALSE(api::present(api::memberValue(api::JsonReader(json), name)));
}

static void test_platform_needs_and_advertisement_follow_the_same_flags() {
  for (bool bluetooth : {false, true}) {
    for (bool scripting : {false, true}) {
      const LinuxCapabilities platformCaps(bluetooth, scripting);
      const DeviceCapabilities caps = DeviceCapabilities::from({}, nullptr, platformCaps.needs());
      const std::string json = api::capabilitiesJson({}, {}, {}, {}, nullptr, nullptr,
          [&](api::JsonWriter& writer) { platformCaps.write(writer); });
      for (const auto* item = platformCaps.needs(); item->name; ++item) {
        bool advertised = false;
        const auto field = api::memberValue(api::JsonReader(json), item->name);
        if (item->present) TEST_ASSERT_TRUE(field.asBool(advertised));
        else TEST_ASSERT_FALSE(api::present(field));
        TEST_ASSERT_EQUAL(item->present, caps.has(item->name));
        TEST_ASSERT_EQUAL(item->present, advertised);
      }
      TEST_ASSERT_EQUAL(bluetooth, caps.has("ble"));
      TEST_ASSERT_EQUAL(scripting, caps.has("gamepad"));
    }
  }
}

static void test_capability_lists_escape_names() {
  const std::string name = "effect\"\\\n";
  const std::string json = api::capabilitiesJson({name}, {}, {}, {});
  TEST_ASSERT_TRUE(api::isWellFormed(json));
  auto effects = api::memberValue(api::JsonReader(json), "effects");
  TEST_ASSERT_TRUE(effects.enterArray());
  TEST_ASSERT_TRUE(effects.nextElement());
  std::string decoded;
  TEST_ASSERT_TRUE(effects.appendString(decoded));
  TEST_ASSERT_EQUAL_STRING(name.c_str(), decoded.c_str());
}

int main(int, char**) {
  UNITY_BEGIN();
  RUN_TEST(test_linux_needs_remain_valid_but_missing_without_the_platform);
  RUN_TEST(test_platform_needs_and_advertisement_follow_the_same_flags);
  RUN_TEST(test_capability_lists_escape_names);
  RUN_TEST(test_the_table_names_exactly_the_shared_vocabulary);
  RUN_TEST(test_capabilities_json_reports_every_table_entry);
  RUN_TEST(test_the_table_follows_the_hardware);
  RUN_TEST(test_url_and_clip_are_reported_but_not_a_need);
  return UNITY_END();
}
