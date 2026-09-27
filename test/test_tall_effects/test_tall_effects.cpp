#include <algorithm>
#include <cmath>
#include <vector>
#include <unity.h>
#include "core/effects/effects/MoreEffects.h"

using namespace awtrix;
void setUp() {}
void tearDown() {}

static void test_ball_travels_and_bricks_change() {
  for (int w : {32, 53}) for (int h = 8; h <= 16; ++h) {
    Canvas c(w, h);
    BrickBreakerEffect e;
    std::vector<bool> rows(h, false);
    int minBricks = w * 2, maxBricks = 0;
    for (int f = 0; f < 512; ++f) {
      e.render(c, f);
      int bricks = 0, balls = 0;
      for (int y = 0; y < h; ++y) for (int x = 0; x < w; ++x) {
        const auto p = c.getPixel(x, y);
        if (p == 0xFFFFFFu) { rows[y] = true; ++balls; }
        else if (y < 2 && p) ++bricks;
      }
      TEST_ASSERT_EQUAL_INT(1, balls);
      minBricks = std::min(minBricks, bricks);
      maxBricks = std::max(maxBricks, bricks);
    }
    TEST_ASSERT_TRUE(std::count(rows.begin(), rows.end(), true) > h / 2);
    TEST_ASSERT_TRUE(minBricks < maxBricks);
    e.render(c, 0);
    std::vector<uint32_t> first(c.data(), c.data() + c.size());
    e.render(c, 512);
    TEST_ASSERT_EQUAL_UINT32_ARRAY(first.data(), c.data(), c.size());
  }
}

static void test_snake_uses_height_quickly() {
  for (int w : {32, 53}) for (int h = 8; h <= 16; ++h) {
    Canvas c(w, h);
    SnakeEffect e;
    std::vector<bool> rows(h, false);
    for (int f = 0; f < 2 * h; ++f) {
      e.render(c, f);
      for (int y = 0; y < h; ++y) for (int x = 0; x < w; ++x)
        if (c.getPixel(x, y)) rows[y] = true;
    }
    TEST_ASSERT_EQUAL_INT(h, std::count(rows.begin(), rows.end(), true));
  }
}

static double radius(Canvas& c) {
  double sum = 0; int count = 0;
  const double rx = (c.width() - 1) / 2.0, ry = (c.height() - 1) / 2.0;
  for (int y = 0; y < c.height(); ++y) for (int x = 0; x < c.width(); ++x) {
    if (!c.getPixel(x, y)) continue;
    sum += std::hypot((x - rx) / rx, (y - ry) / ry); ++count;
  }
  TEST_ASSERT_TRUE(count > 0);
  return sum / count;
}

static void test_swirls_span_and_travel() {
  for (int h : {8, 11, 16}) for (int w : {32, 53}) {
    Canvas c(w, h);
    SwirlInEffect in; SwirlOutEffect out;
    for (IEffect* e : {static_cast<IEffect*>(&in), static_cast<IEffect*>(&out)}) {
      e->render(c, 0);
      int left = w, right = -1;
      for (int y = 0; y < h; ++y) for (int x = 0; x < w; ++x)
        if (c.getPixel(x, y)) { left = std::min(left, x); right = std::max(right, x); }
      TEST_ASSERT_TRUE(right - left + 1 > w * .7);
      const double before = radius(c);
      e->render(c, 12);
      const double after = radius(c);
      TEST_ASSERT_TRUE(e == &in ? after < before : after > before);
    }
  }
}

static void test_determinism_and_no_writes_outside_canvas() {
  BrickBreakerEffect brick; SnakeEffect snake; SwirlInEffect in; SwirlOutEffect out;
  for (int w : {32, 53}) for (int h = 8; h <= 16; ++h) {
    std::vector<uint32_t> storage(w * h + 2, 0xDEADBEEFu);
    Canvas c(w, h, storage.data() + 1), other(w, h);
    for (IEffect* e : {static_cast<IEffect*>(&brick), static_cast<IEffect*>(&snake),
                       static_cast<IEffect*>(&in), static_cast<IEffect*>(&out)}) {
      for (int64_t f : {0LL, 1LL, 12LL, 127LL, 511LL, 512LL, 2147483648LL, 900000000000LL}) {
        e->render(c, f);
        e->render(other, f + 97);
        e->render(other, f);
        TEST_ASSERT_EQUAL_UINT32_ARRAY(c.data(), other.data(), c.size());
        TEST_ASSERT_EQUAL_HEX32(0xDEADBEEFu, storage.front());
        TEST_ASSERT_EQUAL_HEX32(0xDEADBEEFu, storage.back());
      }
    }
  }
}

int main(int, char**) {
  UNITY_BEGIN();
  RUN_TEST(test_ball_travels_and_bricks_change);
  RUN_TEST(test_snake_uses_height_quickly);
  RUN_TEST(test_swirls_span_and_travel);
  RUN_TEST(test_determinism_and_no_writes_outside_canvas);
  return UNITY_END();
}
