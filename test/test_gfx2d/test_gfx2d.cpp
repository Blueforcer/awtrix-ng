#include <unity.h>

#include <vector>

#include "core/render/Color.h"
#include "core/render/Gfx2d.h"
#include "core/render/PaletteStore.h"

using namespace awtrix;
using namespace awtrix::render;

void setUp() {}
void tearDown() {}


static void test_progress_spans_from_x0() {
  Canvas c(32, 8);
  drawProgress(c, 50, 0x00FF00u, 0xFFFFFFu, 0);
  TEST_ASSERT_EQUAL_HEX32(0x00FF00u, c.getPixel(0, 7));
  TEST_ASSERT_EQUAL_HEX32(0x00FF00u, c.getPixel(15, 7));
  TEST_ASSERT_EQUAL_HEX32(0xFFFFFFu, c.getPixel(16, 7));
}

static void test_progress_below_zero_draws_nothing() {
  Canvas c(32, 8);
  drawProgress(c, -1, 0x00FF00u, 0xFFFFFFu, 0);
  TEST_ASSERT_EQUAL_HEX32(0u, c.getPixel(0, 7));
}

static void test_progress_honours_x0() {
  Canvas c(32, 8);
  drawProgress(c, 100, 0x00FF00u, 0xFFFFFFu, 8);
  TEST_ASSERT_EQUAL_HEX32(0u, c.getPixel(7, 7));
  TEST_ASSERT_EQUAL_HEX32(0x00FF00u, c.getPixel(8, 7));
}

static void test_bars_all_positive_anchor_at_bottom() {
  Canvas c(32, 8);
  drawBars(c, {4, 8}, 0x00FF00u, true, 0);
  TEST_ASSERT_EQUAL_HEX32(0x00FF00u, c.getPixel(0, 7));
  TEST_ASSERT_EQUAL_HEX32(0u, c.getPixel(0, 3));
  TEST_ASSERT_EQUAL_HEX32(0x00FF00u, c.getPixel(16, 0));
}

static void test_bars_negative_straddle_zero() {
  Canvas c(32, 8);
  drawBars(c, {-4, 4}, 0x00FF00u, true, 0);
  TEST_ASSERT_EQUAL_HEX32(0x00FF00u, c.getPixel(0, 4));
  TEST_ASSERT_EQUAL_HEX32(0u, c.getPixel(0, 0));
}

static void test_bars_capped_at_16_points() {
  Canvas c(32, 8);
  std::vector<int> many(40, 8);
  drawBars(c, many, 0x00FF00u, false, 0);
  TEST_ASSERT_EQUAL_HEX32(0x00FF00u, c.getPixel(0, 0));
}

static void test_line_needs_two_points() {
  Canvas c(32, 8);
  drawLineChart(c, {5}, 0x00FF00u, true, 0);
  bool lit = false;
  for (std::size_t i = 0; i < c.size(); ++i) lit |= c.data()[i] != 0;
  TEST_ASSERT_FALSE(lit);
}

static void test_line_spans_min_max() {
  Canvas c(32, 8);
  drawLineChart(c, {0, 8}, 0x00FF00u, true, 0);
  TEST_ASSERT_EQUAL_HEX32(0x00FF00u, c.getPixel(0, 7));
  TEST_ASSERT_EQUAL_HEX32(0x00FF00u, c.getPixel(31, 0));
}


static ColorRamp redToBlue() {
  const uint32_t stops[2] = {0xFF0000u, 0x0000FFu};
  ColorRamp r;
  r.pal = paletteFromStopList(stops, 2);
  return r;
}

static void test_bars_take_colour_from_value() {
  Canvas c(32, 8);
  const ColorRamp ramp = redToBlue();
  drawBars(c, {4, 8}, ColorSource(0xFFFFFFu, &ramp), true, 0);
  TEST_ASSERT_EQUAL_HEX32(0x0000FFu, c.getPixel(16, 0));
  const uint32_t low = c.getPixel(0, 7);
  TEST_ASSERT_TRUE(low != 0u);
  TEST_ASSERT_TRUE(low != 0x0000FFu);
}

static void test_bars_without_ramp_stay_flat() {
  Canvas c(32, 8);
  drawBars(c, {4, 8}, 0x00FF00u, true, 0);
  TEST_ASSERT_EQUAL_HEX32(0x00FF00u, c.getPixel(0, 7));
  TEST_ASSERT_EQUAL_HEX32(0x00FF00u, c.getPixel(16, 0));
}

static void test_progress_ramps_along_the_bar() {
  Canvas c(32, 8);
  const ColorRamp ramp = redToBlue();
  drawProgress(c, 100, ColorSource(0xFFFFFFu, &ramp), 0x111111u, 0);
  TEST_ASSERT_EQUAL_HEX32(0xFF0000u, c.getPixel(0, 7));
  TEST_ASSERT_EQUAL_HEX32(0x0000FFu, c.getPixel(31, 7));
}

static void test_progress_fill_edge_carries_the_value() {
  Canvas c(32, 8);
  const ColorRamp ramp = redToBlue();
  drawProgress(c, 50, ColorSource(0xFFFFFFu, &ramp), 0x111111u, 0);
  TEST_ASSERT_EQUAL_HEX32(0xFF0000u, c.getPixel(0, 7));
  const uint32_t edge = c.getPixel(15, 7);
  TEST_ASSERT_TRUE(color::red(edge) < 0xFF && color::blue(edge) > 0);
  TEST_ASSERT_EQUAL_HEX32(0x111111u, c.getPixel(16, 7));
}

static void test_progress_track_ignores_the_ramp() {
  Canvas c(32, 8);
  const ColorRamp ramp = redToBlue();
  drawProgress(c, 50, ColorSource(0xFFFFFFu, &ramp), 0x111111u, 0);
  TEST_ASSERT_EQUAL_HEX32(0x111111u, c.getPixel(31, 7));
}

static void test_line_takes_colour_from_value() {
  Canvas c(32, 8);
  const ColorRamp ramp = redToBlue();
  drawLineChart(c, {0, 8}, ColorSource(0xFFFFFFu, &ramp), true, 0);
  TEST_ASSERT_EQUAL_HEX32(0xFF0000u, c.getPixel(0, 7));
}

int main(int, char**) {
  UNITY_BEGIN();
  RUN_TEST(test_progress_spans_from_x0);
  RUN_TEST(test_progress_below_zero_draws_nothing);
  RUN_TEST(test_progress_honours_x0);
  RUN_TEST(test_bars_all_positive_anchor_at_bottom);
  RUN_TEST(test_bars_negative_straddle_zero);
  RUN_TEST(test_bars_capped_at_16_points);
  RUN_TEST(test_line_needs_two_points);
  RUN_TEST(test_line_spans_min_max);
  RUN_TEST(test_bars_take_colour_from_value);
  RUN_TEST(test_bars_without_ramp_stay_flat);
  RUN_TEST(test_progress_ramps_along_the_bar);
  RUN_TEST(test_progress_fill_edge_carries_the_value);
  RUN_TEST(test_progress_track_ignores_the_ramp);
  RUN_TEST(test_line_takes_colour_from_value);
  return UNITY_END();
}
