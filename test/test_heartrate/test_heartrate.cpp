#include <unity.h>

#include "bluetooth/HeartRateMeasurement.h"

using awtrix::bluetooth::parseHeartRateMeasurement;

void setUp() {}
void tearDown() {}

namespace {

void test_decodes_8_bit_bpm() {
  const uint8_t payload[] = {0x00, 72};
  const auto measurement = parseHeartRateMeasurement(payload, sizeof(payload));
  TEST_ASSERT_TRUE(measurement.valid);
  TEST_ASSERT_EQUAL_UINT16(72, measurement.bpm);
}

void test_decodes_16_bit_little_endian_bpm() {
  const uint8_t payload[] = {0x01, 0x2C, 0x01};
  const auto measurement = parseHeartRateMeasurement(payload, sizeof(payload));
  TEST_ASSERT_TRUE(measurement.valid);
  TEST_ASSERT_EQUAL_UINT16(300, measurement.bpm);
}

void test_rejects_empty_and_null_payloads() {
  const auto empty = parseHeartRateMeasurement(nullptr, 0);
  TEST_ASSERT_FALSE(empty.valid);
  const uint8_t flagsOnly[] = {0x00};
  TEST_ASSERT_FALSE(parseHeartRateMeasurement(flagsOnly, sizeof(flagsOnly)).valid);
}

void test_rejects_short_16_bit_payload() {
  const uint8_t payload[] = {0x01, 0x2C};
  TEST_ASSERT_FALSE(parseHeartRateMeasurement(payload, sizeof(payload)).valid);
}

void test_ignores_optional_fields() {
  const uint8_t payload[] = {0x1E, 74, 0xAA, 0xBB, 0xCC, 0xDD};
  const auto measurement = parseHeartRateMeasurement(payload, sizeof(payload));
  TEST_ASSERT_TRUE(measurement.valid);
  TEST_ASSERT_EQUAL_UINT16(74, measurement.bpm);
}

}

int main(int, char**) {
  UNITY_BEGIN();
  RUN_TEST(test_decodes_8_bit_bpm);
  RUN_TEST(test_decodes_16_bit_little_endian_bpm);
  RUN_TEST(test_rejects_empty_and_null_payloads);
  RUN_TEST(test_rejects_short_16_bit_payload);
  RUN_TEST(test_ignores_optional_fields);
  return UNITY_END();
}
