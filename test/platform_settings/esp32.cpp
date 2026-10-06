#include <unity.h>

#include "core/SettingsBlob.h"
#include "core/apps/BuiltinAppConfig.h"
#include "core/mqtt/HaDiscovery.h"
#include "core/render/ColorGrade.h"
#include "persistence/SystemConfigApply.h"

using namespace awtrix;

void setUp() {}
void tearDown() {}

static void excludes_platform_keys_from_reads_and_writes() {
  const Settings settings;
  const std::string blob = settingsblob::encode(settings);
  for (const char* key : {"clockFace", "calendarAnimation", "bootSound", "musicSource", "enlargeApps"}) {
    TEST_ASSERT_FALSE(api::present(api::memberValue(api::JsonReader(blob), key)));
    TEST_ASSERT_FALSE(settings.read(key).has());
    SettingsError error;
    const std::string patch = std::string("{\"") + key + "\":true}";
    TEST_ASSERT_FALSE(Settings::validateRead(api::JsonReader(patch), error));
    TEST_ASSERT_EQUAL_STRING(key, error.field.c_str());
  }
  std::string config;
  builtinconfig::appendConfigJson(config, "Time", settings, BuiltinAppProfile().clockFaces());
  TEST_ASSERT_NOT_EQUAL(std::string::npos, config.find("timeMode"));
  TEST_ASSERT_EQUAL(std::string::npos, config.find("clockFace"));
  TEST_ASSERT_EQUAL(std::string::npos, config.find("calendarAnimation"));
}

static void restores_supported_preferences_from_a_tc002_backup() {
  constexpr auto backup = R"({"clockFace":"ring","calendarAnimation":false,"bootSound":false,"musicSource":"microphone","brightness":37,"volume":23,"weekdayBar":{"show":false}})";
  SettingsError error;
  TEST_ASSERT_TRUE(Settings::validateRead(api::JsonReader(backup), error, Settings::UnknownKeys::Skip));
  Settings restored;
  settingsblob::apply(restored, backup);
  TEST_ASSERT_EQUAL_INT(37, restored.brightness);
  TEST_ASSERT_EQUAL_INT(23, restored.volume);
  TEST_ASSERT_FALSE(restored.weekdayBar.show);
  TEST_ASSERT_FALSE(restored.dateWeekdayBar.show);
  Settings loaded;
  settingsblob::apply(loaded, settingsblob::encode(restored));
  TEST_ASSERT_EQUAL_STRING(settingsblob::encode(restored).c_str(), settingsblob::encode(loaded).c_str());
}

static void restores_system_config_without_tls_fields() {
  DeviceConfig config;
  config.wifiSsid = "current-network";
  config.wifiPass = "current-password";
  sysconfig::ApplyError error;
  int applied = 0;
  constexpr auto backup = R"({"wifiSsid":"other-network","mqttHost":"broker.test","mqttEnabled":true,"mqttTls":true,"mqttTlsPin":"ignored-platform-value"})";
  TEST_ASSERT_TRUE(sysconfig::apply(config, api::JsonReader(backup), applied, error,
                                   sysconfig::Origin::Restore));
  TEST_ASSERT_TRUE(config.mqttEnabled);
  TEST_ASSERT_EQUAL_STRING("broker.test", config.mqttHost.c_str());
  TEST_ASSERT_EQUAL_STRING("current-network", config.wifiSsid.c_str());
  TEST_ASSERT_EQUAL_STRING("current-password", config.wifiPass.c_str());
  std::string json;
  api::JsonWriter writer(json);
  writer.beginObject();
  config.write(writer);
  writer.endObject();
  TEST_ASSERT_FALSE(api::present(api::memberValue(api::JsonReader(json), "mqttTls")));
  TEST_ASSERT_FALSE(api::present(api::memberValue(api::JsonReader(json), "mqttTlsPin")));
}

static void discovery_omits_platform_entities() {
  ha::DiscoveryContext context;
  context.prefix = "test-device";
  constexpr ha::Entity entity{"voice", R"("p":"button","name":"Voice")"};
  context.setPlatformEntities(&entity, 1);
  ha::StringSink sink;
  ha::emit(context, sink);
  auto components = api::memberValue(api::JsonReader(sink.str), "cmps");
  TEST_ASSERT_TRUE(api::present(api::memberValue(components, "mat")));
  TEST_ASSERT_FALSE(api::present(api::memberValue(components, "voice")));
}

static void linear_panel_preserves_calibration_and_brightness() {
  render::ColorGrade grade;
  render::GradeParams params;
  params.gamma = 1.0f;
  grade.setParams(params);
  TEST_ASSERT_TRUE(grade.isIdentity());
  TEST_ASSERT_EQUAL_HEX32(0x1234AB, grade.applyPixel(0x1234AB));
  grade.setBrightness(80);
  params.correction = 0xFF8040;
  grade.setGrade(params);
  TEST_ASSERT_EQUAL_HEX32(0x502814, grade.applyPixel(0xFFFFFF));
  grade.setBrightness(0);
  TEST_ASSERT_EQUAL_HEX32(0, grade.applyPixel(0xFFFFFF));
  params.gamma = 2.2f;
  grade.setGrade(params);
  TEST_ASSERT_EQUAL_HEX32(0, grade.applyPixel(0xFFFFFF));
  grade.setBrightness(255);
  TEST_ASSERT_EQUAL_HEX32(0xFF8040, grade.applyPixel(0xFFFFFF));
}

int main() {
  UNITY_BEGIN();
  RUN_TEST(excludes_platform_keys_from_reads_and_writes);
  RUN_TEST(restores_supported_preferences_from_a_tc002_backup);
  RUN_TEST(restores_system_config_without_tls_fields);
  RUN_TEST(discovery_omits_platform_entities);
  RUN_TEST(linear_panel_preserves_calibration_and_brightness);
  return UNITY_END();
}
