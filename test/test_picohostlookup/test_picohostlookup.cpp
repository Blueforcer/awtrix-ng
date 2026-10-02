#include <unity.h>
#include <string>
#include <vector>
#include "platform/rp2040/BoundedHostLookup.h"

namespace {
struct Address {
  uint32_t value = 0;
  Address() = default;
  Address(uint8_t a, uint8_t b, uint8_t c, uint8_t d)
      : value((uint32_t(a) << 24) | (uint32_t(b) << 16) | (uint32_t(c) << 8) | d) {}
  explicit operator uint32_t() const { return value; }
};
struct Wifi {
  bool connected = true;
  std::string answer = "broker";
  std::vector<std::string> queries;
  std::vector<int> timeouts;
  bool isConnected() const { return connected; }
  int hostByName(const char* host, Address& out, int timeout) {
    queries.emplace_back(host);
    timeouts.push_back(timeout);
    if (answer != host) return 0;
    out = Address(192, 0, 2, 4);
    return 1;
  }
};
using Lookup = awtrix::platform::pico::BoundedHostLookup<Address>;

void literals_and_cache_are_keyed_by_host() {
  Wifi wifi;
  Lookup lookup;
  TEST_ASSERT_TRUE(lookup.resolve(wifi, "192.0.2.1"));
  TEST_ASSERT_EQUAL_UINT32(0xc0000201, uint32_t(lookup.address()));
  TEST_ASSERT_TRUE(wifi.queries.empty());
  TEST_ASSERT_TRUE(lookup.resolve(wifi, "broker"));
  TEST_ASSERT_TRUE(lookup.resolve(wifi, "broker"));
  TEST_ASSERT_EQUAL_UINT(1, wifi.queries.size());
  TEST_ASSERT_EQUAL_INT(1000, wifi.timeouts[0]);
  TEST_ASSERT_EQUAL_UINT32(0xc0000204, uint32_t(lookup.address()));
  lookup.forget();
  TEST_ASSERT_TRUE(lookup.resolve(wifi, "broker"));
  TEST_ASSERT_EQUAL_UINT(2, wifi.queries.size());
}
void local_falls_back_once_with_bounded_calls() {
  Wifi wifi;
  Lookup lookup;
  TEST_ASSERT_TRUE(lookup.resolve(wifi, "broker.local"));
  TEST_ASSERT_EQUAL_UINT(2, wifi.queries.size());
  TEST_ASSERT_EQUAL_STRING("broker.local", wifi.queries[0].c_str());
  TEST_ASSERT_EQUAL_STRING("broker", wifi.queries[1].c_str());
  for (int timeout : wifi.timeouts) TEST_ASSERT_EQUAL_INT(1000, timeout);
  lookup.forget();
  wifi.answer = "broker.local";
  wifi.queries.clear();
  TEST_ASSERT_TRUE(lookup.resolve(wifi, "broker.local"));
  TEST_ASSERT_EQUAL_UINT(1, wifi.queries.size());
}
void failure_and_wifi_loss_cannot_reuse_stale_address() {
  Wifi wifi;
  Lookup lookup;
  TEST_ASSERT_TRUE(lookup.resolve(wifi, "broker"));
  wifi.connected = false;
  TEST_ASSERT_FALSE(lookup.resolve(wifi, "broker"));
  TEST_ASSERT_EQUAL_INT(int(awtrix::net::LinkError::NoWifi), int(lookup.error()));
  TEST_ASSERT_EQUAL_UINT32(0, uint32_t(lookup.address()));
  wifi.connected = true;
  TEST_ASSERT_FALSE(lookup.resolve(wifi, "missing.local"));
  TEST_ASSERT_EQUAL_UINT(3, wifi.queries.size());
  TEST_ASSERT_EQUAL_INT(int(awtrix::net::LinkError::HostNotFound), int(lookup.error()));
  TEST_ASSERT_FALSE(lookup.resolve(wifi, ""));
  TEST_ASSERT_FALSE(lookup.resolve(wifi, "0.0.0.0"));
  TEST_ASSERT_EQUAL_UINT(3, wifi.queries.size());
  TEST_ASSERT_TRUE(lookup.resolve(wifi, "broker"));
  TEST_ASSERT_EQUAL_INT(int(awtrix::net::LinkError::None), int(lookup.error()));
}
}
void setUp() {}
void tearDown() {}
int main() {
  UNITY_BEGIN();
  RUN_TEST(literals_and_cache_are_keyed_by_host);
  RUN_TEST(local_falls_back_once_with_bounded_calls);
  RUN_TEST(failure_and_wifi_loss_cannot_reuse_stale_address);
  return UNITY_END();
}
