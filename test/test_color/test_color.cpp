#include <unity.h>

#include "core/render/Color.h"

using namespace awtrix;

void setUp() {}
void tearDown() {}

static void test_fromHex_six_digits_with_hash() {
  TEST_ASSERT_EQUAL_HEX32(0xFF0000u, color::fromHex("#FF0000"));
  TEST_ASSERT_EQUAL_HEX32(0x00FF00u, color::fromHex("#00ff00"));
  TEST_ASSERT_EQUAL_HEX32(0x1234ABu, color::fromHex("#1234AB"));
}

static void test_fromHex_six_digits_without_hash() {
  TEST_ASSERT_EQUAL_HEX32(0x0000FFu, color::fromHex("0000FF"));
  TEST_ASSERT_EQUAL_HEX32(0xFFFFFFu, color::fromHex("ffffff"));
}

static void test_fromHex_three_digit_shorthand() {
  TEST_ASSERT_EQUAL_HEX32(0xFF0000u, color::fromHex("#F00"));
  TEST_ASSERT_EQUAL_HEX32(0xFFFFFFu, color::fromHex("FFF"));
  TEST_ASSERT_EQUAL_HEX32(0x112233u, color::fromHex("#123"));
}

static void test_fromHex_invalid_returns_fallback() {
  TEST_ASSERT_EQUAL_HEX32(0x000000u, color::fromHex("nothex"));
  TEST_ASSERT_EQUAL_HEX32(0xABCDEFu, color::fromHex("zz", 0xABCDEFu));
  TEST_ASSERT_EQUAL_HEX32(0x000000u, color::fromHex(""));
}

static void test_fromRgb_clamps() {
  TEST_ASSERT_EQUAL_HEX32(0xFF0000u, color::fromRgb(255, 0, 0));
  TEST_ASSERT_EQUAL_HEX32(0xFFFFFFu, color::fromRgb(300, 300, 300));
  TEST_ASSERT_EQUAL_HEX32(0x000000u, color::fromRgb(-5, -1, -100));
}

static void test_fromHsv_primaries() {
  TEST_ASSERT_EQUAL_HEX32(0xFF0000u, color::fromHsv(0, 100, 100));
  TEST_ASSERT_EQUAL_HEX32(0x00FF00u, color::fromHsv(120, 100, 100));
  TEST_ASSERT_EQUAL_HEX32(0x0000FFu, color::fromHsv(240, 100, 100));
  TEST_ASSERT_EQUAL_HEX32(0xFFFF00u, color::fromHsv(60, 100, 100));
  TEST_ASSERT_EQUAL_HEX32(0x00FFFFu, color::fromHsv(180, 100, 100));
  TEST_ASSERT_EQUAL_HEX32(0xFF00FFu, color::fromHsv(300, 100, 100));
}

static void test_fromHsv_white_and_black() {
  TEST_ASSERT_EQUAL_HEX32(0xFFFFFFu, color::fromHsv(0, 0, 100));
  TEST_ASSERT_EQUAL_HEX32(0x000000u, color::fromHsv(0, 100, 0));
}

static void test_fromHsv_hue_wraps() {
  TEST_ASSERT_EQUAL_HEX32(color::fromHsv(0, 100, 100), color::fromHsv(360, 100, 100));
  TEST_ASSERT_EQUAL_HEX32(color::fromHsv(120, 100, 100), color::fromHsv(-240, 100, 100));
}

static void test_fromKelvin_warm_vs_cold() {
  uint32_t warm = color::fromKelvin(2000);
  uint32_t cold = color::fromKelvin(10000);
  TEST_ASSERT_TRUE(color::red(warm) >= color::blue(warm));
  TEST_ASSERT_TRUE(color::blue(cold) >= color::red(cold));
}

static void test_scale8_endpoints() {
  TEST_ASSERT_EQUAL_UINT8(200, color::scale8(200, 255));
  TEST_ASSERT_EQUAL_UINT8(0, color::scale8(200, 0));
  TEST_ASSERT_EQUAL_UINT8(255, color::scale8(255, 255));
  TEST_ASSERT_EQUAL_UINT8(128, color::scale8(255, 128));
  TEST_ASSERT_EQUAL_UINT8(0, color::scale8(0, 255));
}

static void test_desaturate_full_keeps_colour() {
  TEST_ASSERT_EQUAL_HEX32(0x1234ABu, color::desaturate(0x1234ABu, 100));
  TEST_ASSERT_EQUAL_HEX32(0x1234ABu, color::desaturate(0x1234ABu, 250));
}

static void test_desaturate_zero_is_luma_grey() {
  const uint32_t red = color::desaturate(0xFF0000u, 0);
  TEST_ASSERT_EQUAL_HEX32(0x4C4C4Cu, red);
  const uint32_t green = color::desaturate(0x00FF00u, 0);
  TEST_ASSERT_EQUAL_HEX32(0x959595u, green);
  const uint32_t blue = color::desaturate(0x0000FFu, 0);
  TEST_ASSERT_EQUAL_HEX32(0x1C1C1Cu, blue);
}

static void test_desaturate_fixed_points() {
  TEST_ASSERT_EQUAL_HEX32(0x000000u, color::desaturate(0x000000u, 0));
  TEST_ASSERT_EQUAL_HEX32(0xFFFFFFu, color::desaturate(0xFFFFFFu, 0));
  TEST_ASSERT_EQUAL_HEX32(0x808080u, color::desaturate(0x808080u, 0));
  TEST_ASSERT_EQUAL_HEX32(0x808080u, color::desaturate(0x808080u, 50));
}

static void test_desaturate_partial_sits_between() {
  const uint32_t half = color::desaturate(0xFF0000u, 50);
  TEST_ASSERT_TRUE(color::red(half) > 0x4C && color::red(half) < 0xFF);
  TEST_ASSERT_TRUE(color::green(half) > 0x00 && color::green(half) < 0x4C);
  TEST_ASSERT_EQUAL_UINT8(color::green(half), color::blue(half));
}

static void test_desaturate_clamps_negative() {
  TEST_ASSERT_EQUAL_HEX32(color::desaturate(0xFF0000u, 0), color::desaturate(0xFF0000u, -20));
}

int main(int, char**) {
  UNITY_BEGIN();
  RUN_TEST(test_fromHex_six_digits_with_hash);
  RUN_TEST(test_fromHex_six_digits_without_hash);
  RUN_TEST(test_fromHex_three_digit_shorthand);
  RUN_TEST(test_fromHex_invalid_returns_fallback);
  RUN_TEST(test_fromRgb_clamps);
  RUN_TEST(test_fromHsv_primaries);
  RUN_TEST(test_fromHsv_white_and_black);
  RUN_TEST(test_fromHsv_hue_wraps);
  RUN_TEST(test_fromKelvin_warm_vs_cold);
  RUN_TEST(test_scale8_endpoints);
  RUN_TEST(test_desaturate_full_keeps_colour);
  RUN_TEST(test_desaturate_zero_is_luma_grey);
  RUN_TEST(test_desaturate_fixed_points);
  RUN_TEST(test_desaturate_partial_sits_between);
  RUN_TEST(test_desaturate_clamps_negative);
  return UNITY_END();
}
