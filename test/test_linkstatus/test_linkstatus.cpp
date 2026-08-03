
#include <unity.h>

#include <string>

#include "core/api/JsonWriter.h"
#include "core/api/StateJson.h"
#include "core/net/LinkStatus.h"
#include "core/net/WifiLink.h"

using namespace awtrix;
using namespace awtrix::net;

void setUp() {}
void tearDown() {}

static void test_every_phase_has_a_stable_name() {
  TEST_ASSERT_EQUAL_STRING("disabled", linkPhaseName(LinkPhase::Disabled));
  TEST_ASSERT_EQUAL_STRING("offline", linkPhaseName(LinkPhase::Offline));
  TEST_ASSERT_EQUAL_STRING("connecting", linkPhaseName(LinkPhase::Connecting));
  TEST_ASSERT_EQUAL_STRING("connected", linkPhaseName(LinkPhase::Connected));
}

static void test_every_error_has_a_stable_name() {
  TEST_ASSERT_EQUAL_STRING("noWifi", linkErrorName(LinkError::NoWifi));
  TEST_ASSERT_EQUAL_STRING("hostNotFound", linkErrorName(LinkError::HostNotFound));
  TEST_ASSERT_EQUAL_STRING("refused", linkErrorName(LinkError::Refused));
  TEST_ASSERT_EQUAL_STRING("badCredentials", linkErrorName(LinkError::BadCredentials));
  TEST_ASSERT_EQUAL_STRING("rejected", linkErrorName(LinkError::Rejected));
  TEST_ASSERT_EQUAL_STRING("timeout", linkErrorName(LinkError::Timeout));
  TEST_ASSERT_EQUAL_STRING("lost", linkErrorName(LinkError::Lost));
}

static void test_no_error_renders_empty() {
  TEST_ASSERT_EQUAL_STRING("", linkErrorName(LinkError::None));
}

static void test_a_fresh_status_reads_as_disabled() {
  LinkStatus s;
  TEST_ASSERT_EQUAL_STRING("disabled", linkPhaseName(s.phase));
  TEST_ASSERT_EQUAL_STRING("", linkErrorName(s.error));
  TEST_ASSERT_FALSE(s.enabled);
  TEST_ASSERT_TRUE(s.host.empty());
  TEST_ASSERT_TRUE(s.endpoint.empty());
  TEST_ASSERT_EQUAL_UINT16(0, s.attempts);
  TEST_ASSERT_EQUAL_UINT32(0, s.retryInMs);
  TEST_ASSERT_EQUAL_UINT32(0, s.connects);
}

namespace {

std::string render(const LinkStatus& s) {
  std::string out;
  api::JsonWriter w(out);
  writeLinkStatus(w, s);
  return out;
}

LinkStatus joined() {
  LinkStatus s;
  applyWifiAssoc(s, WifiAssoc::Connected, true, "home", "192.168.1.7");
  return s;
}

}

static void test_a_connected_link_renders_its_endpoint_and_no_error() {
  LinkStatus s;
  s.enabled = true;
  s.phase = LinkPhase::Connected;
  s.host = "carl.local";
  s.endpoint = "192.168.1.42:1883";
  s.connects = 3;
  TEST_ASSERT_EQUAL_STRING(
      "{\"enabled\":true,\"state\":\"connected\",\"host\":\"carl.local\","
      "\"endpoint\":\"192.168.1.42:1883\",\"attempts\":0,\"retryInMs\":0,"
      "\"connects\":3,\"error\":null,\"lastError\":null}",
      render(s).c_str());
}

static void test_an_unresolvable_host_reports_why_and_when() {
  LinkStatus s;
  s.enabled = true;
  s.phase = LinkPhase::Offline;
  s.error = LinkError::HostNotFound;
  s.host = "carl.local";
  s.attempts = 4;
  s.retryInMs = 40000;
  TEST_ASSERT_EQUAL_STRING(
      "{\"enabled\":true,\"state\":\"offline\",\"host\":\"carl.local\",\"endpoint\":\"\","
      "\"attempts\":4,\"retryInMs\":40000,\"connects\":0,\"error\":\"hostNotFound\",\"lastError\":null}",
      render(s).c_str());
}

static void test_mqtt_switched_off_renders_as_disabled() {
  LinkStatus s;
  TEST_ASSERT_EQUAL_STRING(
      "{\"enabled\":false,\"state\":\"disabled\",\"host\":\"\",\"endpoint\":\"\","
      "\"attempts\":0,\"retryInMs\":0,\"connects\":0,\"error\":null,\"lastError\":null}",
      render(s).c_str());
}

static void test_the_host_is_json_escaped() {
  LinkStatus s;
  s.host = "a\"b";
  TEST_ASSERT_TRUE(render(s).find("\"host\":\"a\\\"b\"") != std::string::npos);
}

static void test_wifi_without_credentials_is_disabled() {
  LinkStatus s;
  applyWifiAssoc(s, WifiAssoc::Disconnected, false, "", "");
  TEST_ASSERT_EQUAL(LinkPhase::Disabled, s.phase);
  TEST_ASSERT_EQUAL(LinkError::None, s.error);
  TEST_ASSERT_FALSE(s.enabled);
}

static void test_a_wifi_join_reports_the_ssid_and_address() {
  const LinkStatus s = joined();
  TEST_ASSERT_EQUAL(LinkPhase::Connected, s.phase);
  TEST_ASSERT_EQUAL(LinkError::None, s.error);
  TEST_ASSERT_TRUE(s.enabled);
  TEST_ASSERT_EQUAL_STRING("home", s.host.c_str());
  TEST_ASSERT_EQUAL_STRING("192.168.1.7", s.endpoint.c_str());
  TEST_ASSERT_EQUAL_UINT32(1, s.connects);
}

static void test_a_dropped_wifi_link_reports_lost() {
  LinkStatus s = joined();
  applyWifiAssoc(s, WifiAssoc::Disconnected, true, "home", "");
  TEST_ASSERT_EQUAL(LinkPhase::Offline, s.phase);
  TEST_ASSERT_EQUAL(LinkError::Lost, s.error);
  TEST_ASSERT_TRUE(s.endpoint.empty());
}

static void test_a_wrong_password_reports_bad_credentials() {
  LinkStatus s;
  applyWifiAssoc(s, WifiAssoc::AuthFailed, true, "home", "");
  TEST_ASSERT_EQUAL(LinkPhase::Offline, s.phase);
  TEST_ASSERT_EQUAL(LinkError::BadCredentials, s.error);
}

static void test_a_missing_network_reports_host_not_found() {
  LinkStatus s;
  applyWifiAssoc(s, WifiAssoc::NoSsidFound, true, "home", "");
  TEST_ASSERT_EQUAL(LinkError::HostNotFound, s.error);
}

// The radio only says "disconnected" from the second poll on, so the reason the join failed has
// to survive every later observation.
static void test_the_reason_a_join_failed_survives_later_polls() {
  LinkStatus s;
  applyWifiAssoc(s, WifiAssoc::AuthFailed, true, "home", "");
  applyWifiAssoc(s, WifiAssoc::Disconnected, true, "home", "");
  applyWifiAssoc(s, WifiAssoc::Idle, true, "home", "");
  TEST_ASSERT_EQUAL(LinkError::BadCredentials, s.error);
}

static void test_polling_a_down_link_does_not_inflate_the_attempt_count() {
  LinkStatus s = joined();
  for (int i = 0; i < 20; ++i) applyWifiAssoc(s, WifiAssoc::Disconnected, true, "home", "");
  TEST_ASSERT_EQUAL_UINT16(0, s.attempts);
  noteWifiRetry(s, 5000);
  noteWifiRetry(s, 5000);
  TEST_ASSERT_EQUAL_UINT16(2, s.attempts);
  TEST_ASSERT_EQUAL_UINT32(5000, s.retryInMs);
}

// A flapping access point is the thing worth spotting, so every fresh association counts once.
static void test_reconnecting_counts_a_second_association_and_clears_the_attempts() {
  LinkStatus s = joined();
  applyWifiAssoc(s, WifiAssoc::Disconnected, true, "home", "");
  noteWifiRetry(s, 5000);
  applyWifiAssoc(s, WifiAssoc::Connected, true, "home", "192.168.1.7");
  TEST_ASSERT_EQUAL_UINT32(2, s.connects);
  TEST_ASSERT_EQUAL_UINT16(0, s.attempts);
  TEST_ASSERT_EQUAL_UINT32(0, s.retryInMs);
}

static void test_staying_connected_does_not_count_twice() {
  LinkStatus s = joined();
  applyWifiAssoc(s, WifiAssoc::Connected, true, "home", "192.168.1.7");
  TEST_ASSERT_EQUAL_UINT32(1, s.connects);
}

static void test_a_join_in_progress_reports_connecting_without_an_error() {
  LinkStatus s;
  applyWifiAssoc(s, WifiAssoc::Joining, true, "home", "");
  TEST_ASSERT_EQUAL(LinkPhase::Connecting, s.phase);
  TEST_ASSERT_EQUAL(LinkError::None, s.error);
}

// While Wi-Fi is down nothing can reach the API to ask why, so the reason has to be there to read
// once the link is back - otherwise the error field is only ever set when it is unreachable.
static void test_the_reason_for_an_outage_survives_the_reconnect() {
  LinkStatus s = joined();
  applyWifiAssoc(s, WifiAssoc::Disconnected, true, "home", "");
  applyWifiAssoc(s, WifiAssoc::Connected, true, "home", "192.168.1.7");
  TEST_ASSERT_EQUAL(LinkPhase::Connected, s.phase);
  TEST_ASSERT_EQUAL(LinkError::None, s.error);
  TEST_ASSERT_EQUAL(LinkError::Lost, s.lastError);
}

static void test_a_link_that_never_failed_has_no_last_error() {
  TEST_ASSERT_EQUAL(LinkError::None, joined().lastError);
}

static void test_wifi_renders_the_same_shape_as_mqtt() {
  TEST_ASSERT_EQUAL_STRING(
      "{\"enabled\":true,\"state\":\"connected\",\"host\":\"home\","
      "\"endpoint\":\"192.168.1.7\",\"attempts\":0,\"retryInMs\":0,"
      "\"connects\":1,\"error\":null,\"lastError\":null}",
      render(joined()).c_str());
}

int main(int, char**) {
  UNITY_BEGIN();
  RUN_TEST(test_every_phase_has_a_stable_name);
  RUN_TEST(test_every_error_has_a_stable_name);
  RUN_TEST(test_no_error_renders_empty);
  RUN_TEST(test_a_fresh_status_reads_as_disabled);
  RUN_TEST(test_a_connected_link_renders_its_endpoint_and_no_error);
  RUN_TEST(test_an_unresolvable_host_reports_why_and_when);
  RUN_TEST(test_mqtt_switched_off_renders_as_disabled);
  RUN_TEST(test_the_host_is_json_escaped);
  RUN_TEST(test_wifi_without_credentials_is_disabled);
  RUN_TEST(test_a_wifi_join_reports_the_ssid_and_address);
  RUN_TEST(test_a_dropped_wifi_link_reports_lost);
  RUN_TEST(test_a_wrong_password_reports_bad_credentials);
  RUN_TEST(test_a_missing_network_reports_host_not_found);
  RUN_TEST(test_the_reason_a_join_failed_survives_later_polls);
  RUN_TEST(test_polling_a_down_link_does_not_inflate_the_attempt_count);
  RUN_TEST(test_reconnecting_counts_a_second_association_and_clears_the_attempts);
  RUN_TEST(test_staying_connected_does_not_count_twice);
  RUN_TEST(test_a_join_in_progress_reports_connecting_without_an_error);
  RUN_TEST(test_the_reason_for_an_outage_survives_the_reconnect);
  RUN_TEST(test_a_link_that_never_failed_has_no_last_error);
  RUN_TEST(test_wifi_renders_the_same_shape_as_mqtt);
  return UNITY_END();
}
