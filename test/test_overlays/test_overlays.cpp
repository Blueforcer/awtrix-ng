#include <set>
#include <vector>

#include <unity.h>

#include "core/effects/overlays/RainOverlay.h"
#include "core/effects/overlays/SnowOverlay.h"
#include "core/effects/overlays/WeatherOverlays.h"

using namespace awtrix;

void setUp() {}
void tearDown() {}

namespace {

int litPixels(const Canvas& c) {
  int n = 0;
  for (int y = 0; y < c.height(); ++y)
    for (int x = 0; x < c.width(); ++x)
      if (c.getPixel(x, y) != 0) ++n;
  return n;
}

std::set<int> litColumns(const Canvas& c) {
  std::set<int> cols;
  for (int y = 0; y < c.height(); ++y)
    for (int x = 0; x < c.width(); ++x)
      if (c.getPixel(x, y) != 0) cols.insert(x);
  return cols;
}

}

static void test_rain_draws_drops_over_content() {
  Canvas c(32, 8);
  c.clear(0x000000u);
  RainOverlay r;
  int lit = 0;
  for (long f = 0; f < 8 && lit == 0; ++f) {
    r.render(c, f);
    lit = litPixels(c);
  }
  TEST_ASSERT_TRUE(lit > 0);
  TEST_ASSERT_EQUAL_STRING("rain", r.id().c_str());
}

static void test_rain_is_scattered_not_a_diagonal() {
  RainOverlay r;
  Canvas c(32, 8);
  c.clear(0);
  r.render(c, 0);
  const std::set<int> now = litColumns(c);
  TEST_ASSERT_TRUE(now.size() < 32u);

  Canvas c2(32, 8);
  c2.clear(0);
  r.render(c2, 14 * 3);
  TEST_ASSERT_TRUE(litColumns(c2) != now);
}

static void test_snow_draws_flakes() {
  SnowOverlay s;
  int lit = 0;
  Canvas c(32, 8);
  for (long f = 0; f < 16 && lit == 0; ++f) {
    c.clear(0x000000u);
    s.render(c, f);
    lit = litPixels(c);
  }
  TEST_ASSERT_TRUE(lit > 0);
  TEST_ASSERT_EQUAL_STRING("snow", s.id().c_str());
}

static void test_snow_flakes_sway_sideways() {
  SnowOverlay s;
  std::set<int> colsSeen;
  for (long f = 0; f < 24; ++f) {
    Canvas c(32, 8);
    c.clear(0);
    s.render(c, f);
    for (int col : litColumns(c)) colsSeen.insert(col);
  }
  bool odd = false;
  for (int col : colsSeen) odd = odd || (col % 2) == 1;
  TEST_ASSERT_TRUE(odd);
}

static void test_thunder_flashes_at_irregular_intervals() {
  ThunderOverlay t;
  std::vector<long> flashes;
  for (long f = 0; f < 3000; ++f) {
    Canvas c(32, 8);
    c.clear(0);
    t.render(c, f);
    bool full = true;
    for (int x = 0; x < c.width() && full; ++x) full = c.getPixel(x, 4) != 0;
    if (full) flashes.push_back(f);
  }
  TEST_ASSERT_TRUE(flashes.size() >= 2u);
  std::set<long> gaps;
  for (std::size_t i = 1; i < flashes.size(); ++i) gaps.insert(flashes[i] - flashes[i - 1]);
  TEST_ASSERT_TRUE(gaps.size() > 1u);
}

static void test_frost_is_static_and_hugs_the_edges() {
  FrostOverlay fr;
  Canvas a(32, 8), b(32, 8);
  a.clear(0);
  b.clear(0);
  fr.render(a, 0);
  fr.render(b, 5000);
  for (int y = 0; y < 8; ++y)
    for (int x = 0; x < 32; ++x) TEST_ASSERT_EQUAL_HEX32(a.getPixel(x, y), b.getPixel(x, y));
  bool top = false, bottom = false;
  for (int x = 0; x < 32; ++x) {
    top = top || a.getPixel(x, 0) != 0;
    bottom = bottom || a.getPixel(x, 7) != 0;
    TEST_ASSERT_EQUAL_HEX32(0u, a.getPixel(x, 3));
    TEST_ASSERT_EQUAL_HEX32(0u, a.getPixel(x, 4));
  }
  TEST_ASSERT_TRUE(top);
  TEST_ASSERT_TRUE(bottom);
}

static int diagonalLinks(IEffect& e, int frames) {
  int links = 0;
  for (long f = 0; f < frames; ++f) {
    Canvas c(32, 8);
    c.clear(0);
    e.render(c, f * 3);
    for (int y = 1; y < 8; ++y)
      for (int x = 0; x < 32; ++x) {
        if (c.getPixel(x, y) == 0 || c.getPixel(x, y - 1) != 0) continue;
        const bool left = x > 0 && c.getPixel(x - 1, y - 1) != 0;
        const bool right = x < 31 && c.getPixel(x + 1, y - 1) != 0;
        if (left || right) ++links;
      }
  }
  return links;
}

static void test_storm_slants_while_rain_falls_plumb() {
  StormOverlay st;
  RainOverlay r;
  const int storm = diagonalLinks(st, 20);
  const int rain = diagonalLinks(r, 20);
  TEST_ASSERT_TRUE(storm > 2 * rain);
}

static void test_storm_is_denser_than_drizzle() {
  DrizzleOverlay d;
  StormOverlay st;
  int drizzle = 0, storm = 0;
  for (long f = 0; f < 200; f += 10) {
    Canvas c(32, 8);
    c.clear(0);
    d.render(c, f);
    drizzle += litPixels(c);
    c.clear(0);
    st.render(c, f);
    storm += litPixels(c);
  }
  TEST_ASSERT_TRUE(storm > 2 * drizzle);
}

int main(int, char**) {
  UNITY_BEGIN();
  RUN_TEST(test_rain_draws_drops_over_content);
  RUN_TEST(test_rain_is_scattered_not_a_diagonal);
  RUN_TEST(test_snow_draws_flakes);
  RUN_TEST(test_snow_flakes_sway_sideways);
  RUN_TEST(test_thunder_flashes_at_irregular_intervals);
  RUN_TEST(test_frost_is_static_and_hugs_the_edges);
  RUN_TEST(test_storm_slants_while_rain_falls_plumb);
  RUN_TEST(test_storm_is_denser_than_drizzle);
  return UNITY_END();
}
