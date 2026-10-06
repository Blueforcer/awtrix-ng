#include <unity.h>
#include "persistence/SystemConfigApi.h"
#include "../../src/persistence/DeviceConfigJson.cpp"
#include "../../src/persistence/SystemConfigApply.cpp"
#include "../../src/persistence/SystemConfigApi.cpp"

using namespace awtrix;
void setUp() {}
void tearDown() {}

void test_update_commits_valid_config_and_redacts_secrets() {
  DeviceConfig config;
  config.wifiPass = "private-value";
  int commits = 0;
  const auto result = api::systemConfigRequest("PUT", R"({"hostname":"kitchen"})", false,
      config, [&] { ++commits; return true; });
  TEST_ASSERT_EQUAL_INT(200, result.status);
  TEST_ASSERT_EQUAL_STRING("kitchen", config.hostname.c_str());
  TEST_ASSERT_EQUAL_INT(1, commits);
  TEST_ASSERT_TRUE(result.body.find("private-value") == std::string::npos);
  TEST_ASSERT_TRUE(result.body.find("\"hostname\":\"kitchen\"") != std::string::npos);
}

void test_rejected_requests_never_commit_or_partially_apply() {
  DeviceConfig config;
  config.hostname = "before";
  int commits = 0;
  auto commit = [&] { ++commits; return true; };
  auto result = api::systemConfigRequest("PUT", "{broken", false, config, commit);
  TEST_ASSERT_EQUAL_INT(400, result.status);
  result = api::systemConfigRequest("PUT", R"({"hostname":"after","minBrightness":200,"maxBrightness":10})", false, config, commit);
  TEST_ASSERT_EQUAL_INT(422, result.status);
  TEST_ASSERT_EQUAL_STRING("before", config.hostname.c_str());
  result = api::systemConfigRequest("POST", "{}", false, config, commit);
  TEST_ASSERT_EQUAL_INT(405, result.status);
  TEST_ASSERT_EQUAL_INT(0, commits);
}

void test_reads_obey_secret_permission_and_writes_always_redact() {
  DeviceConfig config;
  config.wifiPass = "private-value";
  auto commit = [] { return true; };
  auto result = api::systemConfigRequest("GET", "", false, config, commit);
  TEST_ASSERT_TRUE(result.body.find("private-value") == std::string::npos);
  result = api::systemConfigRequest("GET", "", true, config, commit);
  TEST_ASSERT_TRUE(result.body.find("private-value") != std::string::npos);
  result = api::systemConfigRequest("PUT", "{}", true, config, commit);
  TEST_ASSERT_TRUE(result.body.find("private-value") == std::string::npos);
}

void test_pending_storage_is_reported_without_losing_live_configuration() {
  DeviceConfig config;
  const auto result = api::systemConfigRequest("PUT", R"({"hostname":"pending"})", false,
      config, [] { return false; });
  TEST_ASSERT_EQUAL_INT(507, result.status);
  TEST_ASSERT_EQUAL_STRING("pending", config.hostname.c_str());
  TEST_ASSERT_TRUE(result.body.find("insufficientStorage") != std::string::npos);
}

void test_nonobject_update_is_rejected_without_persistence() {
  DeviceConfig config;
  int commits = 0;
  for (const auto* body : {"[]", "null", "42", "\"value\""}) {
    const auto result = api::systemConfigRequest("PUT", body, false,
        config, [&] { ++commits; return true; });
    TEST_ASSERT_EQUAL_INT_MESSAGE(422, result.status, body);
  }
  TEST_ASSERT_EQUAL_INT(0, commits);
}

void test_width_updates_stay_within_one_row_of_panels_without_partial_application() {
  DeviceConfig config;
  int commits = 0;
  auto commit = [&] { ++commits; return true; };
  auto result = api::systemConfigRequest("PUT", R"({"panelWidth":64,"panels":3,"hostname":"x"})", false, config, commit);
  TEST_ASSERT_EQUAL_INT(422, result.status);
  TEST_ASSERT_TRUE(result.body.find("\"panelWidth\"") != std::string::npos);
  TEST_ASSERT_TRUE(result.body.find("panelWidth") != std::string::npos);
  TEST_ASSERT_EQUAL_INT(32, config.panelWidth);
  TEST_ASSERT_EQUAL_INT(1, config.panels);
  TEST_ASSERT_TRUE(config.hostname != "x");
  TEST_ASSERT_EQUAL_INT(0, commits);
  result = api::systemConfigRequest("PUT", R"({"panelWidth":64,"panels":2})", false, config, commit);
  TEST_ASSERT_EQUAL_INT(200, result.status);
  TEST_ASSERT_EQUAL_INT(1024, config.matrixLayout().ledCount());
  TEST_ASSERT_EQUAL_INT(kMatrixHeight, config.matrixLayout().height());
}

void test_a_panel_height_is_no_setting() {
  DeviceConfig config;
  int commits = 0;
  auto commit = [&] { ++commits; return true; };
  auto result = api::systemConfigRequest("PUT", R"({"panelHeight":16,"hostname":"kitchen"})", false, config, commit);
  TEST_ASSERT_EQUAL_INT(200, result.status);
  TEST_ASSERT_EQUAL_STRING("kitchen", config.hostname.c_str());
  TEST_ASSERT_EQUAL_INT(kMatrixHeight, config.matrixLayout().height());
  result = api::systemConfigRequest("GET", "", false, config, commit);
  TEST_ASSERT_TRUE(result.body.find("panelHeight") == std::string::npos);
  int applied = 0;
  sysconfig::ApplyError error;
  TEST_ASSERT_TRUE(sysconfig::apply(config, api::JsonReader(R"({"panelWidth":16,"panels":4,"panelHeight":32})"),
      applied, error, sysconfig::Origin::Restore));
  TEST_ASSERT_EQUAL_INT(64, config.matrixLayout().width());
  TEST_ASSERT_EQUAL_INT(kMatrixHeight, config.matrixLayout().height());
}

DeviceConfig fixedDisplayConfig() {
  DeviceConfig config;
  config.panelConfigurable = false;
  config.gpioConfigurable = false;
  return config;
}

void test_configurable_platform_lists_panel_and_pin_fields() {
  DeviceConfig config;
  const auto result = api::systemConfigRequest("GET", "", false, config, [] { return true; });
  for (const char* key : {"\"panelWidth\"", "\"rotate\"", "\"pinMatrix\"", "\"pinAmpEnable\""})
    TEST_ASSERT_TRUE_MESSAGE(result.body.find(key) != std::string::npos, key);
  TEST_ASSERT_TRUE(config.offers("panelWidth"));
  TEST_ASSERT_TRUE(config.offers("notAConfigKey"));
}

void test_fixed_display_saves_without_panel_or_pin_fields() {
  DeviceConfig config = fixedDisplayConfig();
  int commits = 0;
  auto commit = [&] { ++commits; return true; };
  auto result = api::systemConfigRequest(
      "PUT", R"({"hostname":"clock","ntpServer":"de.pool.ntp.org"})", false, config, commit);
  TEST_ASSERT_EQUAL_INT(200, result.status);
  TEST_ASSERT_EQUAL_STRING("clock", config.hostname.c_str());
  result = api::systemConfigRequest(
      "PUT", R"({"panelWidth":64,"panels":2,"rotate":true,"pinMatrix":99,"mqttHost":"broker"})",
      false, config, commit);
  TEST_ASSERT_EQUAL_INT(200, result.status);
  TEST_ASSERT_EQUAL_INT(32, config.panelWidth);
  TEST_ASSERT_EQUAL_INT(1, config.panels);
  TEST_ASSERT_FALSE(config.rotate);
  TEST_ASSERT_EQUAL_INT(pins::activeProfile().defaults.matrix, config.pinMatrix);
  TEST_ASSERT_EQUAL_STRING("broker", config.mqttHost.c_str());
  TEST_ASSERT_EQUAL_INT(2, commits);
  result = api::systemConfigRequest("GET", "", true, config, commit);
  for (const char* key : {"\"panelWidth\"", "\"panels\"", "\"panelHeight\"", "\"panelWiring\"",
                          "\"mirror\"", "\"rotate\"", "\"pinMatrix\"", "\"pinAmpEnable\""})
    TEST_ASSERT_TRUE_MESSAGE(result.body.find(key) == std::string::npos, key);
  TEST_ASSERT_TRUE(result.body.find("\"hostname\":\"clock\"") != std::string::npos);
  TEST_ASSERT_TRUE(result.body.find("\"swapButtons\"") != std::string::npos);
  TEST_ASSERT_FALSE(config.offers("pinMatrix"));
}

void test_fixed_display_restores_a_panel_backup_without_its_geometry_or_pins() {
  DeviceConfig config = fixedDisplayConfig();
  int applied = 0;
  sysconfig::ApplyError error;
  TEST_ASSERT_TRUE(sysconfig::apply(config, api::JsonReader(
      R"({"panelWidth":32,"panels":1,"pinMatrix":48,"pinI2sBclk":4,)"
      R"("mqttEnabled":true,"mqttHost":"broker","hostname":"kitchen"})"),
      applied, error, sysconfig::Origin::Restore));
  TEST_ASSERT_EQUAL_INT(3, applied);
  TEST_ASSERT_TRUE(config.mqttEnabled);
  TEST_ASSERT_EQUAL_STRING("broker", config.mqttHost.c_str());
  TEST_ASSERT_EQUAL_STRING("kitchen", config.hostname.c_str());
  TEST_ASSERT_EQUAL_INT(32, config.panelWidth);
  TEST_ASSERT_EQUAL_INT(pins::activeProfile().defaults.i2sBclk, config.pinI2sBclk);
}

void test_shared_route_uses_presence_of_secrets_and_ignores_other_paths() {
  struct Reply final : api::Reply {
    api::HttpResult result;
    void send(const api::HttpResult& response, bool = false) override { result = response; }
    void header(const char*, const std::string&) override {}
    void chunk(const char*, std::size_t) override {}
    bool sendFile(const std::string&, const char*) override { return false; }
  } reply;
  DeviceConfig config;
  config.wifiPass = "private-value";
  const std::string method = "GET", path = "/api/v1/system", other = "/api/v1/system/other", body;
  bool secrets = false;
  const auto query = [&](const char* name, std::string&) { return secrets && std::string(name) == "secrets"; };
  int commits = 0;
  const auto commit = [&] { ++commits; return true; };
  TEST_ASSERT_TRUE(api::routeSystemConfig({method, path, body, query, {}, {}}, reply, config, commit));
  TEST_ASSERT_TRUE(reply.result.body.find("private-value") == std::string::npos);
  secrets = true;
  TEST_ASSERT_TRUE(api::routeSystemConfig({method, path, body, query, {}, {}}, reply, config, commit));
  TEST_ASSERT_TRUE(reply.result.body.find("private-value") != std::string::npos);
  TEST_ASSERT_FALSE(api::routeSystemConfig({method, other, body, query, {}, {}}, reply, config, commit));
  TEST_ASSERT_EQUAL_INT(0, commits);
}

int main() {
  UNITY_BEGIN();
  RUN_TEST(test_shared_route_uses_presence_of_secrets_and_ignores_other_paths);
  RUN_TEST(test_update_commits_valid_config_and_redacts_secrets);
  RUN_TEST(test_rejected_requests_never_commit_or_partially_apply);
  RUN_TEST(test_reads_obey_secret_permission_and_writes_always_redact);
  RUN_TEST(test_pending_storage_is_reported_without_losing_live_configuration);
  RUN_TEST(test_nonobject_update_is_rejected_without_persistence);
  RUN_TEST(test_width_updates_stay_within_one_row_of_panels_without_partial_application);
  RUN_TEST(test_a_panel_height_is_no_setting);
  RUN_TEST(test_configurable_platform_lists_panel_and_pin_fields);
  RUN_TEST(test_fixed_display_saves_without_panel_or_pin_fields);
  RUN_TEST(test_fixed_display_restores_a_panel_backup_without_its_geometry_or_pins);
  return UNITY_END();
}
