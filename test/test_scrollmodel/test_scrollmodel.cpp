#include <unity.h>

#include "core/render/ScrollModel.h"

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

ResolvedScroll make(ScrollMode mode, ScrollDirection direction = ScrollDirection::Left,
                    ScrollEntry entry = ScrollEntry::Inline, int textWidth = 60, int speed = 100,
                    ScrollWhenFits whenFits = ScrollWhenFits::Static, int holdMs = 1000) {
  ScrollDefaults d;
  d.mode = mode;
  d.direction = direction;
  d.entry = entry;
  d.speed = speed;
  d.gap = 8;
  d.whenFits = whenFits;
  d.holdMs = holdMs;

  ScrollLayout l;
  l.text = inkOf(textWidth);
  l.startX = 9;
  l.availWidth = 32 - 9;
  l.canvasWidth = 32;
  return resolve(ScrollSpec{}, d, l);
}

void run(ScrollModel& m, long fromMs, long untilMs) {
  for (long t = fromMs + 20; t <= untilMs; t += 20) m.advance(t);
}

}

void setUp() {}
void tearDown() {}

static void test_long_stall_does_not_teleport_the_text() {
  ScrollModel m;
  m.reset(make(ScrollMode::Wrap, ScrollDirection::Left, ScrollEntry::Offscreen), 0);
  m.advance(20);
  const float before = m.x();
  m.advance(1020);
  const float step = before - m.x();
  TEST_ASSERT_FLOAT_WITHIN_MESSAGE(0.001f, 0.5f, step,
                                   "a stalled frame is still one frame of travel, not a catch-up");
}

static void test_a_second_draw_in_the_same_frame_does_not_move_the_text_twice() {
  ScrollModel m;
  m.reset(make(ScrollMode::Wrap, ScrollDirection::Left, ScrollEntry::Offscreen), 0);
  m.advance(20);
  const float once = m.x();
  m.advance(20);
  TEST_ASSERT_EQUAL_FLOAT(once, m.x());
}

static void test_inline_entry_starts_at_the_rest_anchor() {
  ScrollModel m;
  m.reset(make(ScrollMode::Wrap), 0);
  TEST_ASSERT_EQUAL_FLOAT(9.f, m.x());
}

static void test_offscreen_entry_starts_outside_the_panel() {
  ScrollModel m;
  m.reset(make(ScrollMode::Wrap, ScrollDirection::Left, ScrollEntry::Offscreen), 0);
  TEST_ASSERT_EQUAL_FLOAT(32.f, m.x());
}

static void test_holds_before_moving() {
  ScrollModel m;
  m.reset(make(ScrollMode::Wrap), 0);
  m.advance(500);
  TEST_ASSERT_EQUAL_FLOAT(9.f, m.x());
  TEST_ASSERT_FALSE(m.moving());

  m.advance(1500);
  TEST_ASSERT_TRUE(m.x() < 9.f);
  TEST_ASSERT_TRUE(m.moving());
}

static void test_offscreen_entry_skips_the_hold() {
  ScrollModel m;
  m.reset(make(ScrollMode::Wrap, ScrollDirection::Left, ScrollEntry::Offscreen), 0);
  m.advance(200);
  TEST_ASSERT_TRUE_MESSAGE(m.x() < 32.f, "a hold on an empty panel is a pause before nothing");
}

static void test_speed_is_half_a_pixel_per_frame_at_100_percent() {
  ScrollModel m;
  m.reset(make(ScrollMode::Wrap), 0);
  m.advance(1000);
  const float before = m.x();
  run(m, 1000, 2000);
  TEST_ASSERT_FLOAT_WITHIN(0.001f, 25.f, before - m.x());
}

static void test_200_percent_steps_exactly_one_pixel_per_frame() {
  ScrollModel m;
  m.reset(make(ScrollMode::Wrap, ScrollDirection::Left, ScrollEntry::Inline, 60, 200), 0);
  m.advance(1000);
  const float before = m.x();
  m.advance(1020);
  TEST_ASSERT_FLOAT_WITHIN(0.001f, 1.f, before - m.x());
}

static void test_frame_length_does_not_change_the_step() {
  ScrollModel m;
  m.reset(make(ScrollMode::Wrap), 0);
  m.advance(1000);
  const float before = m.x();
  m.advance(1005);
  m.advance(1200);
  m.advance(1224);
  TEST_ASSERT_FLOAT_WITHIN(0.001f, 1.5f, before - m.x());
}

static void test_zero_speed_freezes() {
  ScrollModel m;
  m.reset(make(ScrollMode::Wrap, ScrollDirection::Left, ScrollEntry::Inline, 60, 0), 0);
  run(m, 0, 5000);
  TEST_ASSERT_EQUAL_FLOAT(9.f, m.x());
}

static void test_static_mode_never_moves() {
  ScrollModel m;
  m.reset(make(ScrollMode::Static), 0);
  run(m, 0, 5000);
  TEST_ASSERT_EQUAL_FLOAT(9.f, m.x());
  TEST_ASSERT_EQUAL_INT(0, m.cycles());
}

static void test_fitting_text_never_moves() {
  ScrollModel m;
  m.reset(make(ScrollMode::Wrap, ScrollDirection::Left, ScrollEntry::Inline, 10), 0);
  run(m, 0, 5000);
  TEST_ASSERT_EQUAL_FLOAT(9.f, m.x());
  TEST_ASSERT_EQUAL_INT(0, m.cycles());
}

static void test_when_fits_scroll_moves_text_that_fits() {
  ScrollModel m;
  m.reset(make(ScrollMode::Wrap, ScrollDirection::Left, ScrollEntry::Inline, 10, 100,
               ScrollWhenFits::Scroll),
          0);
  run(m, 0, 3000);
  TEST_ASSERT_TRUE(m.x() < 9.f);
}

static void test_wrap_returns_to_the_rest_anchor_and_counts_a_cycle() {
  ScrollModel m;
  m.reset(make(ScrollMode::Wrap), 0);
  long t = 0;
  for (int i = 0; i < 400 && m.cycles() == 0; ++i) {
    t += 20;
    m.advance(t);
  }
  TEST_ASSERT_EQUAL_INT(1, m.cycles());
  TEST_ASSERT_EQUAL_FLOAT(9.f, m.x());
  TEST_ASSERT_FALSE(m.moving());
}

static long runToFarEdge(ScrollModel& m) {
  long t = 0;
  while (m.x() > -27.f && t < 120000) {
    t += 20;
    m.advance(t);
  }
  return t;
}

static void test_right_direction_starts_at_the_far_anchor_and_moves_right() {
  ScrollModel m;
  m.reset(make(ScrollMode::Wrap, ScrollDirection::Right), 0);
  TEST_ASSERT_EQUAL_FLOAT(-27.f, m.x());
  m.advance(1500);
  TEST_ASSERT_TRUE(m.x() > -27.f);
}

static void test_bounce_holds_at_the_far_turning_point() {
  ScrollModel m;
  m.reset(make(ScrollMode::Bounce), 0);
  const long t = runToFarEdge(m);
  TEST_ASSERT_EQUAL_FLOAT(-27.f, m.x());
  TEST_ASSERT_FALSE(m.moving());

  m.advance(t + 500);
  TEST_ASSERT_EQUAL_FLOAT(-27.f, m.x());
  m.advance(t + 1500);
  TEST_ASSERT_TRUE_MESSAGE(m.x() > -27.f, "the far turn must release after the hold");
}

static void test_a_zero_hold_turns_a_bounce_without_pausing() {
  ScrollModel m;
  m.reset(make(ScrollMode::Bounce, ScrollDirection::Left, ScrollEntry::Inline, 60, 100,
               ScrollWhenFits::Static, 0),
          0);
  const long t = runToFarEdge(m);
  TEST_ASSERT_EQUAL_FLOAT(-27.f, m.x());
  m.advance(t + 20);
  TEST_ASSERT_TRUE_MESSAGE(m.x() > -27.f, "a zero hold must reverse immediately");
}

static void test_a_zero_hold_starts_an_inline_text_moving_at_once() {
  ScrollModel m;
  m.reset(make(ScrollMode::Wrap, ScrollDirection::Left, ScrollEntry::Inline, 60, 100,
               ScrollWhenFits::Static, 0),
          0);
  TEST_ASSERT_EQUAL_FLOAT(9.f, m.x());
  m.advance(100);
  TEST_ASSERT_TRUE(m.x() < 9.f);
}

static void test_bounce_counts_a_cycle_per_round_trip() {
  ScrollModel m;
  m.reset(make(ScrollMode::Bounce), 0);
  long t = 0;
  while (m.cycles() == 0 && t < 240000) {
    t += 20;
    m.advance(t);
  }
  TEST_ASSERT_EQUAL_INT(1, m.cycles());
  TEST_ASSERT_EQUAL_FLOAT(9.f, m.x());
}

static void test_bounce_does_not_inherit_the_return_leg_across_a_reset() {
  ScrollModel m;
  const ResolvedScroll r = make(ScrollMode::Bounce);
  m.reset(r, 0);

  long t = runToFarEdge(m);
  t += 1500;
  m.advance(t);
  const float before = m.x();
  t += 200;
  m.advance(t);
  TEST_ASSERT_TRUE_MESSAGE(m.x() > before, "precondition: the model is on its return leg");

  m.reset(r, t);
  TEST_ASSERT_EQUAL_FLOAT(9.f, m.x());
  TEST_ASSERT_EQUAL_INT(0, m.cycles());
  m.advance(t + 1200);
  TEST_ASSERT_TRUE_MESSAGE(m.x() < 9.f, "a fresh page must not start on the return leg");
}

static void test_bounce_sweeps_short_text_towards_the_far_anchor() {
  ScrollModel m;
  m.reset(make(ScrollMode::Bounce, ScrollDirection::Left, ScrollEntry::Inline, 8, 100,
               ScrollWhenFits::Scroll),
          0);
  TEST_ASSERT_EQUAL_FLOAT(9.f, m.x());
  m.advance(1500);
  TEST_ASSERT_TRUE_MESSAGE(m.x() > 9.f, "the sweep heads for the opposite endpoint, not left");
}

static void test_loop_folds_without_holding_again() {
  ScrollModel m;
  m.reset(make(ScrollMode::Loop), 0);
  long t = 0;
  while (m.cycles() == 0 && t < 240000) {
    t += 20;
    m.advance(t);
  }
  TEST_ASSERT_EQUAL_INT(1, m.cycles());
  TEST_ASSERT_TRUE_MESSAGE(m.moving(), "a loop must not stop to hold when it folds");
}

int main(int, char**) {
  UNITY_BEGIN();
  RUN_TEST(test_long_stall_does_not_teleport_the_text);
  RUN_TEST(test_a_second_draw_in_the_same_frame_does_not_move_the_text_twice);
  RUN_TEST(test_inline_entry_starts_at_the_rest_anchor);
  RUN_TEST(test_offscreen_entry_starts_outside_the_panel);
  RUN_TEST(test_holds_before_moving);
  RUN_TEST(test_offscreen_entry_skips_the_hold);
  RUN_TEST(test_speed_is_half_a_pixel_per_frame_at_100_percent);
  RUN_TEST(test_200_percent_steps_exactly_one_pixel_per_frame);
  RUN_TEST(test_frame_length_does_not_change_the_step);
  RUN_TEST(test_zero_speed_freezes);
  RUN_TEST(test_static_mode_never_moves);
  RUN_TEST(test_fitting_text_never_moves);
  RUN_TEST(test_when_fits_scroll_moves_text_that_fits);
  RUN_TEST(test_wrap_returns_to_the_rest_anchor_and_counts_a_cycle);
  RUN_TEST(test_right_direction_starts_at_the_far_anchor_and_moves_right);
  RUN_TEST(test_bounce_holds_at_the_far_turning_point);
  RUN_TEST(test_a_zero_hold_turns_a_bounce_without_pausing);
  RUN_TEST(test_a_zero_hold_starts_an_inline_text_moving_at_once);
  RUN_TEST(test_bounce_counts_a_cycle_per_round_trip);
  RUN_TEST(test_bounce_does_not_inherit_the_return_leg_across_a_reset);
  RUN_TEST(test_bounce_sweeps_short_text_towards_the_far_anchor);
  RUN_TEST(test_loop_folds_without_holding_again);
  return UNITY_END();
}
