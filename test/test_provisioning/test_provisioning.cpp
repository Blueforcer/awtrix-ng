#include <unity.h>

#include "core/ProvisioningPolicy.h"

using namespace awtrix;

namespace {

void test_reads_allowed() {
  TEST_ASSERT_TRUE(provisioning::apModeAllows("GET", "/api/v1/system"));
  TEST_ASSERT_TRUE(provisioning::apModeAllows("GET", "/api/v1/device"));
  TEST_ASSERT_TRUE(provisioning::apModeAllows("GET", "/api/v1/system/wifi-scan"));
  TEST_ASSERT_TRUE(provisioning::apModeAllows("GET", "/version"));
}

void test_wifi_setup_write_allowed() {
  TEST_ASSERT_TRUE(provisioning::apModeAllows("PUT", "/api/v1/system"));
}

void test_reboot_allowed() {
  TEST_ASSERT_TRUE(provisioning::apModeAllows("POST", "/api/v1/device/reboot"));
}

void test_runtime_writes_blocked() {
  TEST_ASSERT_FALSE(provisioning::apModeAllows("PATCH", "/api/v1/settings"));
  TEST_ASSERT_FALSE(provisioning::apModeAllows("PUT", "/api/v1/apps/pushed/clock"));
  TEST_ASSERT_FALSE(provisioning::apModeAllows("POST", "/api/v1/notifications"));
  TEST_ASSERT_FALSE(provisioning::apModeAllows("DELETE", "/api/v1/files"));
}

void test_other_device_commands_blocked() {
  TEST_ASSERT_FALSE(provisioning::apModeAllows("POST", "/api/v1/device/factory-reset"));
  TEST_ASSERT_FALSE(provisioning::apModeAllows("POST", "/api/v1/device/sleep"));
  TEST_ASSERT_FALSE(provisioning::apModeAllows("POST", "/api/v1/device/update"));
  TEST_ASSERT_FALSE(provisioning::apModeAllows("POST", "/api/v1/settings/reset"));
}

void test_uploads_blocked() {
  TEST_ASSERT_FALSE(provisioning::apModeAllows("POST", "/api/v1/files"));
  TEST_ASSERT_FALSE(provisioning::apModeAllows("POST", "/update"));
}

}

void setUp() {}
void tearDown() {}

int main(int, char**) {
  UNITY_BEGIN();
  RUN_TEST(test_reads_allowed);
  RUN_TEST(test_wifi_setup_write_allowed);
  RUN_TEST(test_reboot_allowed);
  RUN_TEST(test_runtime_writes_blocked);
  RUN_TEST(test_other_device_commands_blocked);
  RUN_TEST(test_uploads_blocked);
  UNITY_END();
  return 0;
}
