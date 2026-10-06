
#include <unity.h>

#include "core/net/HostName.h"
#include "core/net/DeviceUrl.h"

using namespace awtrix::net;

void setUp() {}
void tearDown() {}

namespace {

bool parses(const char* s, int a, int b, int c, int d) {
  uint32_t out = 0;
  if (!parseIpv4(s, out)) return false;
  return out == ((uint32_t(a) << 24) | (uint32_t(b) << 16) | (uint32_t(c) << 8) | uint32_t(d));
}

}

static void test_literal_ipv4_is_recognised() {
  TEST_ASSERT_TRUE(parses("192.168.1.10", 192, 168, 1, 10));
  TEST_ASSERT_TRUE(parses("0.0.0.0", 0, 0, 0, 0));
  TEST_ASSERT_TRUE(parses("255.255.255.255", 255, 255, 255, 255));
  TEST_ASSERT_TRUE(parses("10.0.0.1", 10, 0, 0, 1));
}

static void test_non_addresses_are_rejected() {
  uint32_t out = 0x01020304;
  TEST_ASSERT_FALSE(parseIpv4("carl.local", out));
  TEST_ASSERT_FALSE(parseIpv4("broker", out));
  TEST_ASSERT_FALSE(parseIpv4("", out));
  TEST_ASSERT_FALSE(parseIpv4("192.168.1", out));
  TEST_ASSERT_FALSE(parseIpv4("192.168.1.10.5", out));
  TEST_ASSERT_FALSE(parseIpv4("192.168.1.256", out));
  TEST_ASSERT_FALSE(parseIpv4("192.168..10", out));
  TEST_ASSERT_FALSE(parseIpv4(".1.2.3", out));
  TEST_ASSERT_FALSE(parseIpv4("192.168.1.10 ", out));
  TEST_ASSERT_FALSE(parseIpv4("192.168.1.a", out));
  TEST_ASSERT_FALSE(parseIpv4("::1", out));
  TEST_ASSERT_EQUAL_HEX32(0x01020304, out);
}

static void test_mdns_names_are_detected_case_insensitively() {
  TEST_ASSERT_TRUE(isMdnsName("carl.local"));
  TEST_ASSERT_TRUE(isMdnsName("carl.LOCAL"));
  TEST_ASSERT_TRUE(isMdnsName("Carl.Local"));
  TEST_ASSERT_TRUE(isMdnsName("a.b.local"));

  TEST_ASSERT_FALSE(isMdnsName("carl"));
  TEST_ASSERT_FALSE(isMdnsName("carl.localdomain"));
  TEST_ASSERT_FALSE(isMdnsName("mylocal"));
  TEST_ASSERT_FALSE(isMdnsName(".local"));
  TEST_ASSERT_FALSE(isMdnsName(""));
  TEST_ASSERT_FALSE(isMdnsName("192.168.1.10"));
}

static void test_mdns_lookup_uses_the_bare_label() {
  TEST_ASSERT_EQUAL_STRING("carl", mdnsLabel("carl.local").c_str());
  TEST_ASSERT_EQUAL_STRING("Carl", mdnsLabel("Carl.LOCAL").c_str());
  TEST_ASSERT_EQUAL_STRING("a.b", mdnsLabel("a.b.local").c_str());
}

static void test_mdns_label_leaves_ordinary_names_alone() {
  TEST_ASSERT_EQUAL_STRING("broker", mdnsLabel("broker").c_str());
  TEST_ASSERT_EQUAL_STRING("mqtt.example.com", mdnsLabel("mqtt.example.com").c_str());
}

static void test_device_url_reaches_the_web_port() {
  TEST_ASSERT_EQUAL_STRING("http://192.168.1.10:8080", deviceUrl("192.168.1.10", 8080).c_str());
  TEST_ASSERT_EQUAL_STRING("http://192.168.1.10:65535", deviceUrl("192.168.1.10", 65535).c_str());
  TEST_ASSERT_EQUAL_STRING("http://192.168.1.10", deviceUrl("192.168.1.10", 80).c_str());
  TEST_ASSERT_EQUAL_STRING("http://192.168.1.10", deviceUrl("192.168.1.10", 0).c_str());
  TEST_ASSERT_EQUAL_STRING("http://192.168.1.10", deviceUrl("192.168.1.10", -1).c_str());
}

static void test_boot_address_shows_a_custom_port() {
  TEST_ASSERT_EQUAL_STRING("192.168.1.10:8080", deviceAddress("192.168.1.10", 8080).c_str());
  TEST_ASSERT_EQUAL_STRING("192.168.1.10", deviceAddress("192.168.1.10", 80).c_str());
  TEST_ASSERT_EQUAL_STRING("192.168.1.10", deviceAddress("192.168.1.10", 0).c_str());
}

int main(int, char**) {
  UNITY_BEGIN();
  RUN_TEST(test_literal_ipv4_is_recognised);
  RUN_TEST(test_non_addresses_are_rejected);
  RUN_TEST(test_mdns_names_are_detected_case_insensitively);
  RUN_TEST(test_mdns_lookup_uses_the_bare_label);
  RUN_TEST(test_mdns_label_leaves_ordinary_names_alone);
  RUN_TEST(test_device_url_reaches_the_web_port);
  RUN_TEST(test_boot_address_shows_a_custom_port);
  return UNITY_END();
}
