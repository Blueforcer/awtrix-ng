#include <unity.h>

#include "core/sensing/BatteryModel.h"
#include "core/apps/SensorFormat.h"
#include "core/net/SignalStrength.h"

using namespace awtrix;

namespace {


void test_battery_thresholds() {
  TEST_ASSERT_FALSE(isLowBattery(0, 0));
  TEST_ASSERT_FALSE(isLowBattery(0, -1));
  TEST_ASSERT_TRUE(isLowBattery(19, 20));
  TEST_ASSERT_FALSE(isLowBattery(20, 20));
  TEST_ASSERT_EQUAL_HEX32(batteryLevelColor(0, false), batteryLevelColor(100, true));
  TEST_ASSERT_NOT_EQUAL(batteryLevelColor(19, false), batteryLevelColor(20, false));
  TEST_ASSERT_NOT_EQUAL(batteryLevelColor(39, false), batteryLevelColor(40, false));
}

void test_wifi_quality_boundaries() {
  TEST_ASSERT_EQUAL_INT(0, net::signalBars(0));
  TEST_ASSERT_EQUAL_INT(4, net::signalBars(-55));
  TEST_ASSERT_EQUAL_INT(3, net::signalBars(-56));
  TEST_ASSERT_EQUAL_INT(3, net::signalBars(-65));
  TEST_ASSERT_EQUAL_INT(2, net::signalBars(-66));
  TEST_ASSERT_EQUAL_INT(2, net::signalBars(-75));
  TEST_ASSERT_EQUAL_INT(1, net::signalBars(-76));
}

void test_cell_volts_applies_divider() {
  TEST_ASSERT_FLOAT_WITHIN(0.01f, 4.15f, cellVoltsFromPinMillivolts(2075, 2.0f));
  TEST_ASSERT_FLOAT_WITHIN(0.01f, 3.70f, cellVoltsFromPinMillivolts(1850, 2.0f));
}

void test_cell_volts_ratio_zero_falls_back_to_default() {
  TEST_ASSERT_FLOAT_WITHIN(0.01f, cellVoltsFromPinMillivolts(2000, kDefaultBatteryDividerRatio),
                           cellVoltsFromPinMillivolts(2000, 0.0f));
  TEST_ASSERT_FLOAT_WITHIN(0.01f, cellVoltsFromPinMillivolts(2000, kDefaultBatteryDividerRatio),
                           cellVoltsFromPinMillivolts(2000, -1.0f));
}

void test_cell_volts_no_battery_pin_is_zero() {
  TEST_ASSERT_FLOAT_WITHIN(0.001f, 0.0f, cellVoltsFromPinMillivolts(-1, 2.0f));
}


void test_soc_endpoints() {
  TEST_ASSERT_EQUAL_UINT8(100, socFromVolts(4.20f));
  TEST_ASSERT_EQUAL_UINT8(0, socFromVolts(3.27f));
}

void test_soc_clamps_and_never_extrapolates() {
  TEST_ASSERT_EQUAL_UINT8(100, socFromVolts(4.60f));
  TEST_ASSERT_EQUAL_UINT8(0, socFromVolts(2.50f));
}

void test_soc_interpolates_between_points() {
  TEST_ASSERT_UINT8_WITHIN(1, 98, socFromVolts(4.175f));
}

void test_soc_is_monotonic() {
  uint8_t prev = 0;
  for (int mv = 3200; mv <= 4300; mv += 10) {
    const uint8_t p = socFromVolts(static_cast<float>(mv) / 1000.0f);
    TEST_ASSERT_TRUE(p >= prev);
    prev = p;
  }
}

void test_half_empty_cell_is_not_full() {
  const float half = cellVoltsFromPinMillivolts(1920, 2.0f);
  TEST_ASSERT_UINT8_WITHIN(2, 50, socFromVolts(half));
}

}

void setUp() {}
void tearDown() {}

int main(int, char**) {
  UNITY_BEGIN();
  RUN_TEST(test_battery_thresholds);
  RUN_TEST(test_wifi_quality_boundaries);
  RUN_TEST(test_cell_volts_applies_divider);
  RUN_TEST(test_cell_volts_ratio_zero_falls_back_to_default);
  RUN_TEST(test_cell_volts_no_battery_pin_is_zero);
  RUN_TEST(test_soc_endpoints);
  RUN_TEST(test_soc_clamps_and_never_extrapolates);
  RUN_TEST(test_soc_interpolates_between_points);
  RUN_TEST(test_soc_is_monotonic);
  RUN_TEST(test_half_empty_cell_is_not_full);
  UNITY_END();
  return 0;
}
