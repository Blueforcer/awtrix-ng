#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <set>
#include <vector>
#include <unity.h>
#include "core/effects/effects/MoreEffects.h"

using namespace awtrix;
void setUp() {}
void tearDown() {}

#define FOR_EACH_SIZE for (int w : {32, 53}) for (int h = 8; h <= 16; ++h)

static int findPixel(const Canvas& c, uint32_t colour, int& x, int& y) {
  int n = 0;
  for (int yy = 0; yy < c.height(); ++yy) for (int xx = 0; xx < c.width(); ++xx)
    if (c.getPixel(xx, yy) == colour) { x = xx; y = yy; ++n; }
  return n;
}

static void test_brick_geometry() {
  FOR_EACH_SIZE {
    Canvas c(w, h);
    BrickBreakerEffect e;
    e.render(c, 0);
    const int rows = h >= 11 ? 3 : 2;
    for (int y = 0; y <= rows; ++y) {
      std::vector<int> runs;
      int run = 0, first = -1, last = -1;
      for (int x = 0; x <= w; ++x) {
        const bool lit = x < w && c.getPixel(x, y) != 0;
        if (lit) { run++; if (first < 0) first = x; last = x; }
        else if (run) { runs.push_back(run); run = 0; }
      }
      if (y == rows) { TEST_ASSERT_EQUAL_INT(0, (int)runs.size()); continue; }
      TEST_ASSERT_EQUAL_INT((w + 1) / 4, (int)runs.size());
      for (int r : runs) TEST_ASSERT_EQUAL_INT(3, r);            // 3 lit ...
      TEST_ASSERT_EQUAL_INT(4 * (int)runs.size() - 1, last - first + 1);  // ... 1 dark per period
      if (y) TEST_ASSERT_NOT_EQUAL(c.getPixel(first, y - 1), c.getPixel(first, y));  // own hue per row
    }
  }
}

static void test_brick_play() {
  FOR_EACH_SIZE {
    Canvas c(w, h);
    BrickBreakerEffect e;
    fx::BrickGame g;
    g.start(w, h, 0);
    const int full = g.remaining;
    std::vector<bool> rows(h, false);
    std::set<int> slopes;
    int minBricks = full, px = -1, py = -1;
    std::vector<uint32_t> prev;
    for (int f = 0; f < fx::kGameRound; ++f) {
      e.render(c, f);
      int bx = 0, by = 0;
      TEST_ASSERT_EQUAL_INT(1, findPixel(c, fx::kBallColour, bx, by));
      TEST_ASSERT_EQUAL_INT(g.x, bx);
      TEST_ASSERT_EQUAL_INT(g.y, by);
      TEST_ASSERT_TRUE(g.brickAt(bx, by) < 0);  // the ball never stands on a live brick
      rows[by] = true;
      if (f && std::abs(by - py) == 1) slopes.insert(std::abs(bx - px));
      if (!prev.empty()) {
        // Brick pixels that vanished since the last frame form exactly one brick, or none.
        std::vector<int> gone;
        for (int y = 0; y < g.rows; ++y) for (int x = 0; x < w; ++x)
          if (prev[y * w + x] && prev[y * w + x] != fx::kBallColour && !c.getPixel(x, y)) gone.push_back(y * w + x);
        TEST_ASSERT_TRUE(gone.empty() || gone.size() == 3);
        if (gone.size() == 3) {
          TEST_ASSERT_EQUAL_INT(gone[0] + 2, gone[2]);
          TEST_ASSERT_EQUAL_INT(0, (gone[0] % w - g.left) % 4);
        }
      }
      prev.assign(c.data(), c.data() + c.size());
      minBricks = std::min(minBricks, g.remaining);
      px = bx; py = by;
      const int before = g.remaining;
      g.tick();
      TEST_ASSERT_TRUE(g.remaining == before || g.remaining == before - 1 || g.remaining == full);
    }
    TEST_ASSERT_TRUE(minBricks < full);
    TEST_ASSERT_TRUE(std::count(rows.begin(), rows.end(), true) > h / 2);
    TEST_ASSERT_TRUE(slopes.count(1) && slopes.count(2));  // 45 degrees and 2 across per 1 down
    e.render(c, 0);
    std::vector<uint32_t> first(c.data(), c.data() + c.size());
    e.render(c, fx::kGameRound);  // a new round starts with a full wall again
    fx::BrickGame next;
    next.start(w, h, 1);
    TEST_ASSERT_EQUAL_INT(full, next.remaining);
    for (int y = 0; y < g.rows; ++y) for (int x = 0; x < w; ++x)
      TEST_ASSERT_EQUAL_HEX32(first[y * w + x], c.getPixel(x, y));
  }
}

static void test_snake_game() {
  FOR_EACH_SIZE {
    Canvas c(w, h);
    SnakeEffect e;
    fx::SnakeGame g;
    g.start(w, h, 0);
    TEST_ASSERT_TRUE(g.len >= 3 && g.len <= 4);
    std::vector<bool> rows(h, false);
    int eats = 0, moves = 0, closer = 0;
    for (int f = 0; f < fx::kGameRound; ++f) {
      e.render(c, f);
      int fx_ = 0, fy = 0;
      TEST_ASSERT_EQUAL_INT(1, findPixel(c, fx::kSnakeFood, fx_, fy));  // food drawn, not covered
      TEST_ASSERT_EQUAL_INT(g.ox + g.foodX, fx_);
      TEST_ASSERT_EQUAL_INT(g.oy + g.foodY, fy);
      int lit = 0;
      for (int y = 0; y < h; ++y) for (int x = 0; x < w; ++x) lit += c.getPixel(x, y) != 0;
      TEST_ASSERT_EQUAL_INT(g.len + 1, lit);  // every segment on its own pixel, plus the food
      for (int i = 0; i < g.len; ++i) {
        TEST_ASSERT_FALSE(g.segX[i] == g.foodX && g.segY[i] == g.foodY);
        for (int j = 0; j < i; ++j) TEST_ASSERT_FALSE(g.segX[i] == g.segX[j] && g.segY[i] == g.segY[j]);
        if (i) TEST_ASSERT_EQUAL_INT(1, std::abs(g.segX[i] - g.segX[i - 1]) + std::abs(g.segY[i] - g.segY[i - 1]));
      }
      rows[g.oy + g.segY[0]] = true;
      const int len = g.len, foodX = g.foodX, foodY = g.foodY;
      const uint32_t games = g.games;
      const int dist = std::abs(g.segX[0] - foodX) + std::abs(g.segY[0] - foodY);
      g.tick();
      if (g.games != games) continue;  // stuck or full: a fresh game
      if (g.len != len) {
        ++eats;
        TEST_ASSERT_EQUAL_INT(len + 1, g.len);
        TEST_ASSERT_TRUE(g.foodX != foodX || g.foodY != foodY);
      } else {
        ++moves;
        closer += std::abs(g.segX[0] - foodX) + std::abs(g.segY[0] - foodY) < dist;
      }
    }
    TEST_ASSERT_TRUE(eats > 3);
    TEST_ASSERT_TRUE(closer * 10 > moves * 7);
    TEST_ASSERT_EQUAL_INT(h, std::count(rows.begin(), rows.end(), true));
  }
}

static void test_snake_body_gradient() {
  Canvas c(32, 8);
  SnakeEffect e;
  e.render(c, 0);
  fx::SnakeGame g;
  g.start(32, 8, 0);
  TEST_ASSERT_NOT_EQUAL(c.getPixel(g.segX[0], g.segY[0]), c.getPixel(g.segX[g.len - 1], g.segY[g.len - 1]));
}

static void test_pingpong_paddles_and_ball() {
  FOR_EACH_SIZE {
    Canvas c(w, h);
    PingPongEffect e;
    fx::PongGame g;
    g.start(w, h, 0);
    const int paddle = h >= 11 ? 4 : 3;
    std::vector<int> ys;
    int contacts = 0;
    for (int f = 0; f < fx::kGameRound; ++f) {
      e.render(c, f);
      int bx = 0, by = 0;
      TEST_ASSERT_EQUAL_INT(1, findPixel(c, fx::kPongBall, bx, by));
      TEST_ASSERT_TRUE(bx >= 1 && bx <= w - 2);
      for (int side : {0, w - 1}) {
        int n = 0, top = -1;
        for (int y = 0; y < h; ++y)
          if (c.getPixel(side, y) == fx::kPongPaddle) { if (top < 0) top = y; ++n; }
        TEST_ASSERT_EQUAL_INT(paddle, n);
        for (int y = top; y < top + paddle; ++y) TEST_ASSERT_EQUAL_HEX32(fx::kPongPaddle, c.getPixel(side, y));
      }
      if (bx == 1) { TEST_ASSERT_EQUAL_HEX32(fx::kPongPaddle, c.getPixel(0, by)); ++contacts; }
      if (bx == w - 2) { TEST_ASSERT_EQUAL_HEX32(fx::kPongPaddle, c.getPixel(w - 1, by)); ++contacts; }
      ys.push_back(by);
    }
    TEST_ASSERT_TRUE(contacts >= 4);
    // Two-tick windows away from the walls: 45 degrees moves 2 rows, the shallow angle 1.
    std::set<int> slopes;
    for (size_t i = 2; i < ys.size(); ++i)
      if (ys[i - 1] > 0 && ys[i - 1] < h - 1) slopes.insert(std::abs(ys[i] - ys[i - 2]));
    TEST_ASSERT_TRUE(slopes.count(1) && slopes.count(2));
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
  BrickBreakerEffect brick; SnakeEffect snake; PingPongEffect pong; SwirlInEffect in; SwirlOutEffect out;
  for (int w : {4, 32, 53, 64, 80}) for (int h = 8; h <= 16; ++h) {
    std::vector<uint32_t> storage(w * h + 2, 0xDEADBEEFu);
    Canvas c(w, h, storage.data() + 1), other(w, h);
    for (IEffect* e : {static_cast<IEffect*>(&brick), static_cast<IEffect*>(&snake), static_cast<IEffect*>(&pong),
                       static_cast<IEffect*>(&in), static_cast<IEffect*>(&out)}) {
      for (int64_t f : {0LL, 1LL, 12LL, 127LL, 300LL, 511LL, 512LL, -5LL, 2147483648LL, 900000000000LL}) {
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
  RUN_TEST(test_brick_geometry);
  RUN_TEST(test_brick_play);
  RUN_TEST(test_snake_game);
  RUN_TEST(test_snake_body_gradient);
  RUN_TEST(test_pingpong_paddles_and_ball);
  RUN_TEST(test_swirls_span_and_travel);
  RUN_TEST(test_determinism_and_no_writes_outside_canvas);
  return UNITY_END();
}
