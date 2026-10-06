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

static void test_weather_never_darkens_the_app() {
  RainOverlay rain; SnowOverlay snow; StormOverlay storm;
  IEffect* all[] = {&rain, &snow, &storm};
  for (IEffect* e : all)
    for (long f = 0; f < 60; ++f) {
      Canvas c(32, 8);
      c.clear(0xC08000u);
      e->render(c, f);
      for (int y = 0; y < 8; ++y)
        for (int x = 0; x < 32; ++x) {
          const uint32_t p = c.getPixel(x, y);
          TEST_ASSERT_TRUE(((p >> 16) & 0xFF) >= 0xC0 && ((p >> 8) & 0xFF) >= 0x80);
        }
    }
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
    for (int x = 0; x < c.width() && full; ++x) full = c.getPixel(x, c.height() / 2) != 0;
    if (full) flashes.push_back(f);
  }
  TEST_ASSERT_TRUE(flashes.size() >= 2u);
  std::set<long> gaps;
  for (std::size_t i = 1; i < flashes.size(); ++i) gaps.insert(flashes[i] - flashes[i - 1]);
  TEST_ASSERT_TRUE(gaps.size() > 1u);
}

static void test_frost_hugs_the_edges_and_shimmers() {
  FrostOverlay fr;
  Canvas a(32, 8), b(32, 8);
  a.clear(0);
  b.clear(0);
  fr.render(a, 0);
  fr.render(b, 60);
  bool top = false, bottom = false, changed = false;
  for (int y = 0; y < 8; ++y)
    for (int x = 0; x < 32; ++x) {
      TEST_ASSERT_EQUAL(a.getPixel(x, y) != 0, b.getPixel(x, y) != 0);
      changed = changed || a.getPixel(x, y) != b.getPixel(x, y);
    }
  for (int x = 0; x < 32; ++x) {
    top = top || a.getPixel(x, 0) != 0;
    bottom = bottom || a.getPixel(x, a.height() - 1) != 0;
    TEST_ASSERT_EQUAL_HEX32(0u, a.getPixel(x, a.height() / 2 - 1));
    TEST_ASSERT_EQUAL_HEX32(0u, a.getPixel(x, a.height() / 2));
  }
  TEST_ASSERT_TRUE(top);
  TEST_ASSERT_TRUE(bottom);
  TEST_ASSERT_TRUE(changed);
}

static void test_frost_rim_grows_with_the_panel() {
  FrostOverlay fr;
  Canvas c(32, 32);
  c.clear(0);
  fr.render(c, 0);
  int rows = 0;
  for (int y = 0; y < 32; ++y) {
    bool lit = false;
    for (int x = 0; x < 32; ++x) lit = lit || c.getPixel(x, y) != 0;
    rows += lit;
  }
  Canvas small(32, 8);
  fr.render(small, 0);
  int smallRows = 0;
  for (int y = 0; y < small.height(); ++y) {
    bool lit = false;
    for (int x = 0; x < small.width(); ++x) lit |= small.getPixel(x, y) != 0;
    smallRows += lit;
  }
  TEST_ASSERT_TRUE(rows > smallRows);
  for (int x = 0; x < 32; ++x) TEST_ASSERT_EQUAL_HEX32(0u, c.getPixel(x, c.height() / 2));
}

static void test_thunder_strike_draws_a_bolt_over_the_text() {
  ThunderOverlay t;
  for (long f = 0; f < 3000; ++f) {
    Canvas c(32, 8);
    c.clear(0x00FF00u);
    t.render(c, f);
    if (c.getPixel(0, c.height() / 2) == 0x00FF00u && c.getPixel(c.width() - 1, c.height() / 2) == 0x00FF00u) continue;
    int bolt = 0, keptGreen = 0;
    for (int y = 0; y < 8; ++y)
      for (int x = 0; x < 32; ++x) {
        const uint32_t p = c.getPixel(x, y);
        bolt += (p & 0xFF) > 0xC0;
        keptGreen += ((p >> 8) & 0xFF) == 0xFF;
      }
    TEST_ASSERT_TRUE(bolt > 0);
    TEST_ASSERT_TRUE(keptGreen > 32 * 8 / 2);
    return;
  }
  TEST_FAIL_MESSAGE("no strike in 3000 frames");
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
  RUN_TEST(test_weather_never_darkens_the_app);
  RUN_TEST(test_rain_is_scattered_not_a_diagonal);
  RUN_TEST(test_snow_draws_flakes);
  RUN_TEST(test_snow_flakes_sway_sideways);
  RUN_TEST(test_thunder_flashes_at_irregular_intervals);
  RUN_TEST(test_frost_hugs_the_edges_and_shimmers);
  RUN_TEST(test_frost_rim_grows_with_the_panel);
  RUN_TEST(test_thunder_strike_draws_a_bolt_over_the_text);
  RUN_TEST(test_storm_slants_while_rain_falls_plumb);
  RUN_TEST(test_storm_is_denser_than_drizzle);
  return UNITY_END();
}
