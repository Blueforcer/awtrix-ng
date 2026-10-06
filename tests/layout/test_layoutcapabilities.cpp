#include <unity.h>
#include "core/api/CapabilitiesJson.h"
#include "core/api/JsonReader.h"
#include "platform/linux/LinuxCapabilities.h"

using namespace awtrix;
void setUp() {}
void tearDown() {}

static void test_advertised_layout_limits_come_from_the_runtime_budget() {
  layout::Limits limits;
  limits.regions = 5;
  limits.preparedBytes = 256 * 1024;
  const LinuxCapabilities caps(false, true, &limits);
  const std::string encoded = api::capabilitiesJson({}, {}, {}, {}, nullptr, nullptr,
      [&](api::JsonWriter& writer) { caps.write(writer); });
  TEST_ASSERT_TRUE(api::isWellFormed(encoded));
  auto layouts = api::memberValue(api::JsonReader(encoded), "layouts");
  long long value = 0;
  TEST_ASSERT_TRUE(api::memberValue(layouts, "version").asLong(value));
  TEST_ASSERT_EQUAL_INT(1, value);
  const auto advertised = api::memberValue(layouts, "limits");
  TEST_ASSERT_TRUE(api::memberValue(advertised, "preparedBytes").asLong(value));
  TEST_ASSERT_EQUAL_UINT32(256 * 1024, value);
  TEST_ASSERT_TRUE(api::memberValue(advertised, "regions").asLong(value));
  TEST_ASSERT_EQUAL_INT(5, value);
  TEST_ASSERT_TRUE(api::memberValue(advertised, "scriptHandlesPerScript").asLong(value));
  TEST_ASSERT_EQUAL_INT(4, value);
}

int main(int, char**) {
  UNITY_BEGIN();
  RUN_TEST(test_advertised_layout_limits_come_from_the_runtime_budget);
  return UNITY_END();
}
