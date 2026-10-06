#include <unity.h>

#include <string>

#include "core/ProvisioningPolicy.h"
#include "core/api/ApiRouter.h"

using namespace awtrix;
using provisioning::Verdict;

namespace {

Verdict admit(const std::string& method, const std::string& path, const std::string& body = {},
              const std::string& overrideMethod = {}, bool hostValid = true,
              bool originValid = true, bool secrets = false) {
  const auto resolved = api::resolveHttpMethod(method, path, overrideMethod);
  return provisioning::admit({resolved.method, path, body, hostValid, originValid,
                               secrets, resolved.error == nullptr});
}

void test_only_setup_reads_allowed() {
  for (const char* path : {"/", "/index.html", "/api/v1/device", "/api/v1/capabilities",
                           "/api/v1/system", "/api/v1/system/wifi-scan"})
    TEST_ASSERT_TRUE_MESSAGE(admit("GET", path) == Verdict::Allow, path);
  for (const char* path : {"/api/v1/logs", "/api/v1/apps", "/api/v1/files", "/api/v1/icons/origins",
                           "/api/v1/scripts/shared", "/api/v1/apps/script/demo", "/ICONS/icon.gif",
                           "/SCRIPTS/demo.ax", "/system.json", "/wifi.json"})
    TEST_ASSERT_TRUE_MESSAGE(admit("GET", path) == Verdict::Refuse, path);
}

void test_only_network_fields_are_writable() {
  for (const char* body : {R"({"wifiSsid":"Home","wifiPass":"secret"})",
                           R"({"wifiSsid":"Cafe","wifiPass":""})", R"({"hostname":"kitchen"})"})
    TEST_ASSERT_TRUE_MESSAGE(admit("PUT", "/api/v1/system", body) == Verdict::Allow, body);
  for (const char* body : {R"({"authEnabled":false})", R"({"netStatic":true})",
                           R"({"wifiSsid":"Home","mqttEnabled":true})",
                           R"({"wifiSsid":"A","wifiSsid":"B"})",
                           R"({"wifiSsid":"A","wifi\u0053sid":"B"})",
                           R"({"wifiSsid":1})", R"({"wifiPass":null})",
                           R"({"hostname":{"name":"other"}})", "[]", "{broken", "{}",
                           R"({"hostname":"a",})", R"({"hostname":"a"} {"hostname":"b"})",
                           R"({"hostname":"unterminated})", R"({"hostname":"a"})" "null"})
    TEST_ASSERT_TRUE_MESSAGE(admit("PUT", "/api/v1/system", body) == Verdict::Refuse, body);
  const std::string oversized = "{\"wifiSsid\":\"" + std::string(1024, 'a') + "\"}";
  TEST_ASSERT_TRUE(admit("PUT", "/api/v1/system", oversized) == Verdict::Refuse);
}

void test_streamed_body_is_checked_after_the_header_guard() {
  TEST_ASSERT_TRUE(admit("PUT", "/api/v1/system") == Verdict::Allow);
  TEST_ASSERT_TRUE(admit("PUT", "/api/v1/system", R"({"mqttPass":"changed"})") == Verdict::Refuse);
}

void test_reboot_and_restore_are_the_only_other_writes() {
  TEST_ASSERT_TRUE(admit("POST", "/api/v1/device/reboot") == Verdict::Allow);
  TEST_ASSERT_TRUE(admit("POST", "/api/v1/restore") == Verdict::Allow);
  for (const char* path : {"/api/v1/device/factory-reset", "/api/v1/device/sleep", "/api/v1/settings/reset",
                           "/api/v1/notifications", "/api/v1/files", "/update",
                           "/api/v1/apps/demo/sounds", "/api/v1/audio/mp3"})
    TEST_ASSERT_TRUE_MESSAGE(admit("POST", path) == Verdict::Refuse, path);
  TEST_ASSERT_TRUE(admit("PATCH", "/api/v1/settings") == Verdict::Refuse);
  TEST_ASSERT_TRUE(admit("DELETE", "/api/v1/files") == Verdict::Refuse);
  TEST_ASSERT_TRUE(admit("OPTIONS", "/api/v1/system") == Verdict::Refuse);
}

void test_restore_keeps_the_setup_boundary() {
  TEST_ASSERT_TRUE(admit("POST", "/api/v1/restore", "", "", false) == Verdict::Refuse);
  TEST_ASSERT_TRUE(admit("POST", "/api/v1/restore", "", "", true, false) == Verdict::Refuse);
  TEST_ASSERT_TRUE(admit("POST", "/api/v1/restore", "", "", true, true, true) == Verdict::Refuse);
  TEST_ASSERT_TRUE(admit("GET", "/api/v1/restore") == Verdict::Refuse);
  TEST_ASSERT_TRUE(admit("PUT", "/api/v1/restore") == Verdict::Refuse);
  TEST_ASSERT_TRUE(admit("POST", "/api/v1/restore", "", "DELETE") == Verdict::Refuse);
  TEST_ASSERT_TRUE(admit("POST", "/api/v1/restore", "", "BOGUS") == Verdict::Refuse);
}

void test_method_overrides_keep_the_same_boundary() {
  TEST_ASSERT_TRUE(admit("POST", "/api/v1/system", R"({"wifiSsid":"Home"})", "PUT") == Verdict::Allow);
  TEST_ASSERT_TRUE(admit("POST", "/api/v1/system", R"({"mqttEnabled":false})", "PUT") == Verdict::Refuse);
  TEST_ASSERT_TRUE(admit("POST", "/api/v1/system", "{}", "DELETE") == Verdict::Refuse);
  TEST_ASSERT_TRUE(admit("GET", "/api/v1/system", "", "PUT") == Verdict::Refuse);
  TEST_ASSERT_TRUE(admit("POST", "/api/v1/restore", "", "PUT") == Verdict::Refuse);
}

void test_redirects_only_read_probes() {
  TEST_ASSERT_TRUE(admit("GET", "/generate_204", "", "", false) == Verdict::Redirect);
  TEST_ASSERT_TRUE(admit("HEAD", "/", "", "", false) == Verdict::Redirect);
  TEST_ASSERT_TRUE(admit("POST", "/api/v1/device/reboot", "", "", false) == Verdict::Refuse);
  TEST_ASSERT_TRUE(admit("PUT", "/api/v1/system", R"({"wifiSsid":"Home"})", "", false) == Verdict::Refuse);
  TEST_ASSERT_TRUE(admit("GET", "/generate_204") == Verdict::Redirect);
  TEST_ASSERT_TRUE(admit("GET", "/api/v1/unknown") == Verdict::Refuse);
}

void test_foreign_origins_and_secret_export_are_refused() {
  TEST_ASSERT_TRUE(admit("GET", "/api/v1/system", "", "", true, false) == Verdict::Refuse);
  TEST_ASSERT_TRUE(admit("PUT", "/api/v1/system", R"({"wifiSsid":"Home"})", "", true, false) == Verdict::Refuse);
  TEST_ASSERT_TRUE(admit("GET", "/api/v1/system", "", "", true, true, true) == Verdict::Refuse);
  TEST_ASSERT_TRUE(admit("GET", "/api/v1/device", "", "", true, true, true) == Verdict::Refuse);
}

void test_authority_is_exact_except_explicit_default_port() {
  TEST_ASSERT_TRUE(provisioning::matchesAuthority("192.168.4.1", "192.168.4.1", true));
  TEST_ASSERT_TRUE(provisioning::matchesAuthority("192.168.4.1:80", "192.168.4.1", true));
  TEST_ASSERT_TRUE(provisioning::matchesAuthority("http://192.168.4.1:80", "http://192.168.4.1", true));
  for (const char* host : {"", "192.168.4.1.evil", "192.168.4.1:81", "192.168.4.1:80@evil", "192.168.4.1:garbage"})
    TEST_ASSERT_FALSE(provisioning::matchesAuthority(host, "192.168.4.1", true));
  TEST_ASSERT_TRUE(provisioning::matchesAuthority("192.168.4.1:8080", "192.168.4.1:8080", false));
  TEST_ASSERT_FALSE(provisioning::matchesAuthority("192.168.4.1", "192.168.4.1:8080", false));
}

}

void setUp() {}
void tearDown() {}

int main(int, char**) {
  UNITY_BEGIN();
  RUN_TEST(test_only_setup_reads_allowed);
  RUN_TEST(test_only_network_fields_are_writable);
  RUN_TEST(test_streamed_body_is_checked_after_the_header_guard);
  RUN_TEST(test_reboot_and_restore_are_the_only_other_writes);
  RUN_TEST(test_restore_keeps_the_setup_boundary);
  RUN_TEST(test_method_overrides_keep_the_same_boundary);
  RUN_TEST(test_redirects_only_read_probes);
  RUN_TEST(test_foreign_origins_and_secret_export_are_refused);
  RUN_TEST(test_authority_is_exact_except_explicit_default_port);
  return UNITY_END();
}
