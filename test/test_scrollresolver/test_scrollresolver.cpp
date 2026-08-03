#include <unity.h>

#include "core/render/ScrollResolver.h"

using namespace awtrix;
using namespace awtrix::render;

namespace {

text::TextMetrics inkOf(int advance) {
  text::TextMetrics m;
  m.advance = advance;
  m.inkLeft = 0;
  m.inkRight = advance > 0 ? advance - 2 : -1;
  return m;
}

ScrollLayout layout(int textWidth = 60, int textOffset = 0) {
  ScrollLayout l;
  l.text = inkOf(textWidth);
  l.startX = 9;
  l.availWidth = 32 - 9;
  l.canvasWidth = 32;
  l.textOffset = textOffset;
  return l;
}

ScrollLayout layoutWithInk(int advance, int inkLeft, int inkRight) {
  ScrollLayout l = layout(advance);
  l.text.inkLeft = inkLeft;
  l.text.inkRight = inkRight;
  return l;
}

}

void setUp() {}
void tearDown() {}

static void test_an_unset_field_falls_back_to_the_device_default() {
  ScrollDefaults d;
  d.mode = ScrollMode::Loop;
  d.speed = 55;
  d.gap = 3;
  d.holdMs = 250;

  ScrollSpec s;
  s.hasMode = true;
  s.mode = ScrollMode::Bounce;

  const ResolvedScroll r = resolve(s, d, layout());
  TEST_ASSERT_EQUAL_INT(static_cast<int>(ScrollMode::Bounce), static_cast<int>(r.mode));
  TEST_ASSERT_EQUAL_FLOAT(55.f, r.speedPercent);
  TEST_ASSERT_EQUAL_INT(3, r.gap);
  TEST_ASSERT_EQUAL_INT(250, r.holdMs);
}

static void test_a_page_overrides_the_default_hold() {
  ScrollDefaults d;
  d.holdMs = 1000;

  ScrollSpec s;
  s.hasHoldMs = true;
  s.holdMs = 0;

  TEST_ASSERT_EQUAL_INT(0, resolve(s, d, layout()).holdMs);
}

static void test_inheritance_is_field_wise_not_all_or_nothing() {
  ScrollDefaults d;
  d.direction = ScrollDirection::Right;
  d.entry = ScrollEntry::Offscreen;
  d.whenFits = ScrollWhenFits::Scroll;

  ScrollSpec s;
  s.hasEntry = true;
  s.entry = ScrollEntry::Inline;

  const ResolvedScroll r = resolve(s, d, layout());
  TEST_ASSERT_EQUAL_INT(static_cast<int>(ScrollEntry::Inline), static_cast<int>(r.entry));
  TEST_ASSERT_EQUAL_INT(static_cast<int>(ScrollDirection::Right), static_cast<int>(r.direction));
  TEST_ASSERT_EQUAL_INT(static_cast<int>(ScrollWhenFits::Scroll), static_cast<int>(r.whenFits));
}

static void test_static_mode_never_animates() {
  ScrollDefaults d;
  d.mode = ScrollMode::Static;
  d.whenFits = ScrollWhenFits::Scroll;
  TEST_ASSERT_FALSE(resolve(ScrollSpec{}, d, layout()).animates());
}

static void test_overflowing_text_animates_in_a_motion_mode() {
  ScrollDefaults d;
  d.mode = ScrollMode::Wrap;
  TEST_ASSERT_TRUE(resolve(ScrollSpec{}, d, layout(60)).animates());
}

static void test_fitting_text_is_still_by_default() {
  ScrollDefaults d;
  d.mode = ScrollMode::Wrap;
  TEST_ASSERT_FALSE(resolve(ScrollSpec{}, d, layout(10)).animates());
}

static void test_when_fits_scroll_animates_text_that_fits() {
  ScrollDefaults d;
  d.mode = ScrollMode::Wrap;
  d.whenFits = ScrollWhenFits::Scroll;
  TEST_ASSERT_TRUE(resolve(ScrollSpec{}, d, layout(10)).animates());
}

static void test_left_anchors() {
  ScrollDefaults d;
  const ResolvedScroll r = resolve(ScrollSpec{}, d, layout(60));
  TEST_ASSERT_EQUAL_FLOAT(9.f, r.xRest);
  TEST_ASSERT_EQUAL_FLOAT(32.f, r.xOff);
  TEST_ASSERT_EQUAL_FLOAT(-60.f, r.xEnd);
}

static void test_right_anchors_mirror_the_geometry() {
  ScrollDefaults d;
  d.direction = ScrollDirection::Right;
  const ResolvedScroll r = resolve(ScrollSpec{}, d, layout(60));
  TEST_ASSERT_EQUAL_FLOAT(-27.f, r.xRest);
  TEST_ASSERT_EQUAL_FLOAT(-60.f, r.xOff);
  TEST_ASSERT_EQUAL_FLOAT(32.f, r.xEnd);
}

static void test_text_offset_participates_in_every_anchor() {
  ScrollDefaults d;
  const ResolvedScroll l = resolve(ScrollSpec{}, d, layout(60, 4));
  TEST_ASSERT_EQUAL_FLOAT(-64.f, l.xEnd);

  d.direction = ScrollDirection::Right;
  const ResolvedScroll r = resolve(ScrollSpec{}, d, layout(60, 4));
  TEST_ASSERT_EQUAL_FLOAT(-31.f, r.xRest);
  TEST_ASSERT_EQUAL_FLOAT(-64.f, r.xOff);
}

static void test_the_far_anchor_docks_the_ink_not_the_advance() {
  ScrollDefaults d;
  d.direction = ScrollDirection::Right;
  const ResolvedScroll r = resolve(ScrollSpec{}, d, layoutWithInk(60, 0, 54));
  TEST_ASSERT_EQUAL_FLOAT(-23.f, r.xRest);
  TEST_ASSERT_EQUAL_FLOAT(-23.f, r.xFar);
  TEST_ASSERT_EQUAL_FLOAT(-60.f, r.xOff);
}

static void test_fitting_is_decided_by_the_ink() {
  ScrollDefaults d;
  d.mode = ScrollMode::Wrap;
  TEST_ASSERT_FALSE(resolve(ScrollSpec{}, d, layoutWithInk(24, 0, 22)).overflows());
  TEST_ASSERT_TRUE(resolve(ScrollSpec{}, d, layoutWithInk(24, 0, 23)).overflows());
}

static void test_bounce_endpoints_are_direction_independent() {
  ScrollDefaults d;
  const ResolvedScroll l = resolve(ScrollSpec{}, d, layout(60, 4));
  TEST_ASSERT_EQUAL_FLOAT(9.f, l.xNear);
  TEST_ASSERT_EQUAL_FLOAT(-31.f, l.xFar);

  d.direction = ScrollDirection::Right;
  const ResolvedScroll r = resolve(ScrollSpec{}, d, layout(60, 4));
  TEST_ASSERT_EQUAL_FLOAT(9.f, r.xNear);
  TEST_ASSERT_EQUAL_FLOAT(-31.f, r.xFar);
}

static void test_loop_period_spans_text_offset_and_gap() {
  ScrollDefaults d;
  d.gap = 3;
  TEST_ASSERT_EQUAL_INT(67, resolve(ScrollSpec{}, d, layout(60, 4)).period);
}

int main(int, char**) {
  UNITY_BEGIN();
  RUN_TEST(test_an_unset_field_falls_back_to_the_device_default);
  RUN_TEST(test_a_page_overrides_the_default_hold);
  RUN_TEST(test_inheritance_is_field_wise_not_all_or_nothing);
  RUN_TEST(test_static_mode_never_animates);
  RUN_TEST(test_overflowing_text_animates_in_a_motion_mode);
  RUN_TEST(test_fitting_text_is_still_by_default);
  RUN_TEST(test_when_fits_scroll_animates_text_that_fits);
  RUN_TEST(test_left_anchors);
  RUN_TEST(test_right_anchors_mirror_the_geometry);
  RUN_TEST(test_text_offset_participates_in_every_anchor);
  RUN_TEST(test_the_far_anchor_docks_the_ink_not_the_advance);
  RUN_TEST(test_fitting_is_decided_by_the_ink);
  RUN_TEST(test_bounce_endpoints_are_direction_independent);
  RUN_TEST(test_loop_period_spans_text_offset_and_gap);
  return UNITY_END();
}
