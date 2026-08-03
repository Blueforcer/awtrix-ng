#include <unity.h>

#include "core/render/ScrollController.h"

using namespace awtrix;

void setUp() {}
void tearDown() {}

static render::ScrollLayout layoutOf(int advance) {
  render::ScrollLayout l;
  l.canvasWidth = 32;
  l.availWidth = 32;
  l.text.advance = advance;
  l.text.inkLeft = 0;
  l.text.inkRight = advance - 1;
  return l;
}

static ScrollSpec movingSpec() {
  ScrollSpec s;
  s.hasHoldMs = true;
  s.holdMs = 0;
  return s;
}

static int64_t run(render::ScrollController& c, int64_t fromMs, int steps) {
  int64_t now = fromMs;
  for (int i = 0; i < steps; ++i) {
    now += 100;
    c.advance(now);
  }
  return now;
}

static void test_identical_content_keeps_its_position() {
  render::ScrollController c;
  const ScrollDefaults defaults;
  c.set(movingSpec(), defaults, layoutOf(50), 0);
  const int64_t now = run(c, 0, 5);

  const float moved = c.x();
  TEST_ASSERT_TRUE_MESSAGE(moved < 0.0f, "an overflowing run should have moved");

  c.set(movingSpec(), defaults, layoutOf(50), now);
  TEST_ASSERT_EQUAL_FLOAT(moved, c.x());
}

static void test_different_metrics_start_over() {
  render::ScrollController c;
  const ScrollDefaults defaults;
  c.set(movingSpec(), defaults, layoutOf(50), 0);
  const int64_t now = run(c, 0, 5);
  TEST_ASSERT_TRUE(c.x() < 0.0f);

  c.set(movingSpec(), defaults, layoutOf(60), now);
  TEST_ASSERT_EQUAL_FLOAT(0.0f, c.x());
}

static void test_restart_rewinds_the_same_content() {
  render::ScrollController c;
  const ScrollDefaults defaults;
  c.set(movingSpec(), defaults, layoutOf(50), 0);
  const int64_t now = run(c, 0, 5);
  TEST_ASSERT_TRUE(c.x() < 0.0f);

  c.restart(now);
  TEST_ASSERT_EQUAL_FLOAT(0.0f, c.x());
}

static void test_wants_time_until_the_run_completes() {
  render::ScrollController c;
  const ScrollDefaults defaults;
  c.set(movingSpec(), defaults, layoutOf(50), 0);

  TEST_ASSERT_TRUE(c.wantsMoreTime(1));
  TEST_ASSERT_FALSE_MESSAGE(c.wantsMoreTime(0), "repeat 0 never holds");

  run(c, 0, 40);
  TEST_ASSERT_TRUE(c.cycles() >= 1);
  TEST_ASSERT_FALSE(c.wantsMoreTime(1));
  TEST_ASSERT_TRUE_MESSAGE(c.wantsMoreTime(c.cycles() + 1), "a higher repeat still holds");
}

static void test_text_that_fits_never_holds() {
  render::ScrollController c;
  const ScrollDefaults defaults;
  c.set(movingSpec(), defaults, layoutOf(10), 0);

  TEST_ASSERT_FALSE(c.resolved().animates());
  TEST_ASSERT_FALSE(c.wantsMoreTime(1));
}

static void test_static_mode_never_holds() {
  render::ScrollController c;
  const ScrollDefaults defaults;
  ScrollSpec spec = movingSpec();
  spec.hasMode = true;
  spec.mode = ScrollMode::Static;
  c.set(spec, defaults, layoutOf(50), 0);

  TEST_ASSERT_FALSE(c.wantsMoreTime(1));
}

static void test_bounce_stays_inside_the_box() {
  render::ScrollController c;
  const ScrollDefaults defaults;
  ScrollSpec spec = movingSpec();
  spec.hasMode = true;
  spec.mode = ScrollMode::Bounce;

  render::ScrollLayout l = layoutOf(50);
  l.startX = 9;
  l.availWidth = 23;
  c.set(spec, defaults, l, 0);

  int64_t now = 0;
  for (int i = 0; i < 120; ++i) {
    now += 100;
    c.advance(now);
    TEST_ASSERT_TRUE_MESSAGE(c.x() <= 9.0f, "bounce must not pass the left edge of the box");
    TEST_ASSERT_TRUE_MESSAGE(c.x() >= static_cast<float>(9 + 23 - 50),
                             "bounce must not pass the right edge of the box");
  }
}

int main(int, char**) {
  UNITY_BEGIN();
  RUN_TEST(test_identical_content_keeps_its_position);
  RUN_TEST(test_different_metrics_start_over);
  RUN_TEST(test_restart_rewinds_the_same_content);
  RUN_TEST(test_wants_time_until_the_run_completes);
  RUN_TEST(test_text_that_fits_never_holds);
  RUN_TEST(test_static_mode_never_holds);
  RUN_TEST(test_bounce_stays_inside_the_box);
  return UNITY_END();
}
