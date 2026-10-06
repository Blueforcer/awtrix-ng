#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <vector>

#include <unity.h>
#include "../Visuals.h"

#include "core/effects/EffectRegistry.h"
#include "core/effects/PlasmaField.h"
#include "core/effects/effects/FadeEffect.h"
#include "core/effects/effects/MoreEffects.h"
#include "core/effects/effects/PlasmaEffect.h"
#include "core/effects/effects/SimulatedEffects.h"
#include "core/effects/effects/TheaterChaseEffect.h"
#include "core/effects/overlays/RainOverlay.h"
#include "core/effects/overlays/SnowOverlay.h"
#include "core/effects/overlays/WeatherOverlays.h"
#include "core/render/Color.h"
#include "core/render/PaletteStore.h"

using namespace awtrix;

void setUp() {}
void tearDown() {}

static void test_registry() {
  EffectRegistry r;
  PlasmaEffect p;
  r.add(&p);
  TEST_ASSERT_EQUAL_UINT(1u, (unsigned)r.size());
  TEST_ASSERT_TRUE(r.find("Plasma") == &p);
  TEST_ASSERT_TRUE(r.find("Nope") == nullptr);
  TEST_ASSERT_TRUE(r.find("") == nullptr);
  r.add(nullptr);
  TEST_ASSERT_EQUAL_UINT(1u, (unsigned)r.size());
}

static void test_registry_lookup_is_case_insensitive() {
  EffectRegistry r;
  PlasmaEffect p;
  r.add(&p);
  TEST_ASSERT_TRUE(r.find("plasma") == &p);
  TEST_ASSERT_TRUE(r.find("PLASMA") == &p);
  TEST_ASSERT_TRUE(r.find("PlAsMa") == &p);
  TEST_ASSERT_EQUAL_UINT(1u, (unsigned)r.names().size());
  TEST_ASSERT_EQUAL_STRING("Plasma", r.names()[0].c_str());
}

static void test_plasma_fills() {
  PlasmaEffect p;
  Canvas c(32, 8);
  p.render(c, 0);
  bool any = false;
  for (int y = 0; y < 8 && !any; ++y)
    for (int x = 0; x < 32 && !any; ++x)
      if (c.getPixel(x, y) != 0) any = true;
  TEST_ASSERT_TRUE(any);
}

static void test_theater_chase_pattern() {
  TheaterChaseEffect t;
  Canvas first(32, 8), next(32, 8);
  t.render(first, 0);
  t.render(next, 1);
  bool lit = false, moved = false;
  for (int y = 0; y < 8; ++y)
    for (int x = 0; x < 32; ++x) {
      lit = lit || first.getPixel(x, y) != 0u;
      moved = moved || first.getPixel(x, y) != next.getPixel(x, y);
    }
  TEST_ASSERT_TRUE(lit && moved);
}

static void test_fade_rows_cycle_through_the_colours() {
  FadeEffect f;
  Canvas c(32, 8);
  f.render(c, 0);
  TEST_ASSERT_EQUAL_HEX32(c.getPixel(0, 0), c.getPixel(31, 0));
  TEST_ASSERT_TRUE(c.getPixel(0, 0) != c.getPixel(0, 4));
  const uint32_t before = c.getPixel(0, 0);
  f.render(c, 40);
  TEST_ASSERT_TRUE(c.getPixel(0, 0) != before);
}

static bool anyLit(Canvas& c) {
  for (int y = 0; y < c.height(); ++y)
    for (int x = 0; x < c.width(); ++x)
      if (c.getPixel(x, y) != 0) return true;
  return false;
}

static void test_more_effects_render_and_are_safe() {
  Canvas c(32, 8);
  ColorWavesEffect waves; waves.render(c, 0); TEST_ASSERT_TRUE(anyLit(c));
  MatrixEffect mtx; mtx.render(c, 5); TEST_ASSERT_TRUE(anyLit(c));
  RadarEffect radar; radar.render(c, 3); TEST_ASSERT_TRUE(anyLit(c));
  PacificaEffect{}.render(c, 7);
  RippleEffect{}.render(c, 9);
  SwirlInEffect{}.render(c, 2);
  SwirlOutEffect{}.render(c, 2);
  FireworksEffect{}.render(c, 11);
  LookingEyesEffect{}.render(c, 4);
  TwinklingStarsEffect{}.render(c, 6);
  CheckerboardEffect{}.render(c, 1);
  MovingLineEffect{}.render(c, 1);
  BrickBreakerEffect brick; brick.render(c, 1); TEST_ASSERT_TRUE(anyLit(c));
  PingPongEffect pong; pong.render(c, 1); TEST_ASSERT_TRUE(anyLit(c));
  SnakeEffect snake; snake.render(c, 1); TEST_ASSERT_TRUE(anyLit(c));
  PlasmaCloudEffect{}.render(c, 1);
  TEST_ASSERT_TRUE(true);
}

static int countLit(const Canvas& c, int rows) {
  int n = 0;
  for (int y = 0; y < rows; ++y)
    for (int x = 0; x < c.width(); ++x)
      if (c.getPixel(x, y) != 0) ++n;
  return n;
}

template <typename Predicate>
static bool findPixel(const Canvas& c, Predicate matches, int& fx, int& fy) {
  for (int y = 0; y < c.height(); ++y)
    for (int x = 0; x < c.width(); ++x)
      if (matches(c.getPixel(x, y))) {
        fx = x;
        fy = y;
        return true;
      }
  return false;
}

static void test_brick_breaker_ball_travels_and_breaks_bricks() {
  BrickBreakerEffect e;
  Canvas c(32, 8);
  e.render(c, 0);
  const int bricksAtStart = test::countPixels(c, [](uint32_t p) { return p && !test::brightNeutral(p); });
  TEST_ASSERT_TRUE(bricksAtStart > 0);
  int rows[8] = {0};
  bool left[32] = {false};
  for (int64_t f = 1; f <= 600; ++f) {
    e.render(c, f);
    int x, y;
    if (findPixel(c, [](uint32_t p) { return test::brightNeutral(p) && (p >> 16) > 224; }, x, y)) {
      rows[y] = 1;
      left[x] = true;
    }
  }
  int rowsVisited = 0, columnsVisited = 0;
  for (int r : rows) rowsVisited += r;
  for (bool b : left) columnsVisited += b;
  TEST_ASSERT_TRUE_MESSAGE(rowsVisited >= c.height() / 2, "ball never left the paddle row");
  TEST_ASSERT_TRUE(columnsVisited >= c.width() / 2);
  TEST_ASSERT_TRUE_MESSAGE(test::countPixels(c, [](uint32_t p) { return p && !test::brightNeutral(p); }) < bricksAtStart, "no brick was broken");
}

static void test_ping_pong_ball_crosses_the_panel() {
  PingPongEffect e;
  Canvas c(32, 8);
  int minX = 32, maxX = -1;
  for (int64_t f = 0; f < 300; ++f) {
    e.render(c, f);
    int x, y;
    if (findPixel(c, test::red, x, y)) {
      minX = std::min(minX, x);
      maxX = std::max(maxX, x);
    }
  }
  TEST_ASSERT_TRUE(minX < c.width() / 4);
  TEST_ASSERT_TRUE(maxX > 3 * c.width() / 4);
}

static void test_snake_hunts_the_apple_and_grows() {
  SnakeEffect e;
  Canvas c(32, 8);
  e.render(c, 0);
  const int startLength = test::countPixels(c, test::green);
  int longest = startLength;
  for (int64_t f = 1; f < 400; ++f) {
    e.render(c, f);
    longest = std::max(longest, test::countPixels(c, test::green));
  }
  TEST_ASSERT_TRUE(startLength > 0);
  TEST_ASSERT_TRUE_MESSAGE(longest > startLength, "snake never ate");
}

static void test_simulations_keep_one_state_per_view() {
  BrickBreakerEffect e;
  Canvas small(32, 8), wide(40, 8);
  e.render(small, 0);
  const int bricks = test::countPixels(small, [](uint32_t p) { return p && !test::brightNeutral(p); });
  for (int64_t f = 1; f <= 600; ++f) {
    e.render(small, f);
    e.render(wide, f);
  }
  TEST_ASSERT_TRUE(test::countPixels(small, [](uint32_t p) { return p && !test::brightNeutral(p); }) < bricks);
}

static void test_simulations_skip_a_long_absence() {
  SnakeEffect e;
  Canvas c(32, 8);
  e.render(c, 0);
  e.render(c, 100000000);
  e.render(c, 100000001);
  TEST_ASSERT_TRUE(countLit(c, 8) > 0);
}

static void* failAllocation(std::size_t) { return nullptr; }
static void test_simulation_without_memory_draws_nothing() {
  const auto allocator = render::frameAllocator();
  render::setFrameAllocator({failAllocation, std::free});
  BrickBreakerEffect e;
  Canvas c(32, 8);
  c.clear(0xFFFFFFu);
  e.render(c, 5);
  render::setFrameAllocator(allocator);
  TEST_ASSERT_EQUAL_INT(0, countLit(c, 8));
}

struct CountingSimulation {
  int steps, resets;
  uint32_t seed;
  void reset(int, int, uint32_t value) { steps = 0; ++resets; seed = value; }
  void step() { ++steps; }
};

static void test_simulation_slots_evict_the_oldest_view_and_bound_catch_up() {
  fx::SimulationSlots<CountingSimulation> slots;
  auto* first = slots.advance(32, 8, 1.0f, 0);
  auto* second = slots.advance(64, 8, 1.0f, 0);
  TEST_ASSERT_TRUE(first != second);
  TEST_ASSERT_EQUAL_PTR(first, slots.advance(32, 8, 1.0f, 3));
  TEST_ASSERT_EQUAL_INT(3, first->steps);
  TEST_ASSERT_EQUAL_PTR(second, slots.advance(96, 8, 1.0f, 0));
  TEST_ASSERT_EQUAL_INT(2, second->resets);
  TEST_ASSERT_EQUAL_PTR(first, slots.advance(32, 8, 1.0f, 1000000));
  TEST_ASSERT_EQUAL_INT(19, first->steps);
  slots.advance(32, 8, 1.0f, 1000000);
  slots.advance(32, 8, 1.0f, 2);
  TEST_ASSERT_EQUAL_INT(19, first->steps);
  slots.advance(32, 8, 1.0f, 3);
  TEST_ASSERT_EQUAL_INT(20, first->steps);
  const uint32_t previousSeed = second->seed;
  TEST_ASSERT_EQUAL_PTR(second, slots.advance(32, 8, 2.0f, 3));
  TEST_ASSERT_EQUAL_INT(3, second->resets);
  TEST_ASSERT_EQUAL_INT(0, second->steps);
  TEST_ASSERT_NOT_EQUAL(previousSeed, second->seed);
}

static int simulationAllocations, simulationReleases;
static void* retrySimulationAllocation(std::size_t size) {
  return ++simulationAllocations == 1 ? nullptr : std::malloc(size);
}
static void releaseSimulation(void* memory) {
  ++simulationReleases;
  std::free(memory);
}
static void test_simulation_allocation_retries_and_keeps_its_release_function() {
  const auto allocator = render::frameAllocator();
  simulationAllocations = simulationReleases = 0;
  render::setFrameAllocator({retrySimulationAllocation, releaseSimulation});
  bool failed, retried, reused;
  {
    fx::SimulationSlots<CountingSimulation> slots;
    failed = slots.advance(32, 8, 1.0f, 0) == nullptr;
    retried = slots.advance(32, 8, 1.0f, 0) != nullptr;
    render::setFrameAllocator(allocator);
    reused = slots.advance(64, 8, 1.0f, 0) != nullptr;
  }
  TEST_ASSERT_TRUE(failed && retried && reused);
  TEST_ASSERT_EQUAL_INT(2, simulationAllocations);
  TEST_ASSERT_EQUAL_INT(1, simulationReleases);
}

static void test_looking_eyes_scale_with_the_panel() {
  Canvas c(64, 16);
  LookingEyesEffect{}.render(c, 4);
  int lit = 0;
  for (int y = 0; y < 16; ++y)
    for (int x = 0; x < 64; ++x) lit += test::brightNeutral(c.getPixel(x, y));
  Canvas small(32, 8);
  LookingEyesEffect{}.render(small, 4);
  int smallLit = 0;
  for (int y = 0; y < 8; ++y)
    for (int x = 0; x < 32; ++x) smallLit += test::brightNeutral(small.getPixel(x, y));
  TEST_ASSERT_EQUAL_INT(smallLit * 4, lit);
}


static EffectSettings withPalette(const char* name, bool blend) {
  EffectSettings s;
  s.ramp.pal = render::paletteByName(name);
  s.ramp.blend = blend;
  return s;
}

static void test_plasma_animates_across_panel_sizes() {
  for (int height : {8, 16, 17, 32}) {
    Canvas first(128, height), later(128, height), repeated(128, height);
    PlasmaEffect plasma;
    plasma.render(first, 7);
    plasma.render(later, 70);
    plasma.render(repeated, 7);
    TEST_ASSERT_TRUE(test::countPixels(first, test::lit) > 0);
    TEST_ASSERT_FALSE(test::sameFrame(first, later));
    TEST_ASSERT_TRUE(test::sameFrame(first, repeated));
  }
}

static void* failAxesAllocation(std::size_t) { return nullptr; }
static void test_plasma_workspace_growth_failure_keeps_previous_storage() {
  fx::Axes axes;
  TEST_ASSERT_TRUE(axes.fits(32, 8));
  float* original = axes.storage;
  const auto allocator = render::frameAllocator();
  render::setFrameAllocator({failAxesAllocation, std::free});
  const bool large = axes.fits(128, 32);
  render::setFrameAllocator(allocator);
  TEST_ASSERT_FALSE(large);
  TEST_ASSERT_EQUAL_PTR(original, axes.storage);
  TEST_ASSERT_TRUE(axes.fits(32, 8));
  TEST_ASSERT_FALSE(axes.fits(128, 33));
}

static void test_effect_without_palette_keeps_own_colours() {
  Canvas a(32, 8), b(32, 8);
  PlasmaEffect p;
  p.render(a, 5);
  PlasmaEffect q;
  q.render(b, 5);
  TEST_ASSERT_EQUAL_HEX32(a.getPixel(0, 0), b.getPixel(0, 0));
  TEST_ASSERT_TRUE(a.getPixel(0, 0) != 0u);
}

static void test_effect_uses_palette_when_set() {
  Canvas plain(32, 8), heat(32, 8);
  PlasmaEffect a;
  a.render(plain, 5);

  PlasmaEffect b;
  b.setSettings(withPalette("Heat", true));
  b.render(heat, 5);

  TEST_ASSERT_TRUE(plain.getPixel(0, 0) != heat.getPixel(0, 0));
  for (int y = 0; y < 8; ++y)
    for (int x = 0; x < 32; ++x) {
      const uint32_t c = heat.getPixel(x, y);
      const uint8_t r = (c >> 16) & 0xFF, g = (c >> 8) & 0xFF, bl = c & 0xFF;
      TEST_ASSERT_TRUE(bl <= r);
      TEST_ASSERT_TRUE(bl <= g || g == 0);
    }
}

static void test_blend_off_gives_hard_bands() {
  Canvas c(32, 8);
  PlasmaEffect e;
  e.setSettings(withPalette("Party", false));
  e.render(c, 3);
  const render::Palette& p = render::namedPalette("Party");
  for (int y = 0; y < 8; ++y)
    for (int x = 0; x < 32; ++x) {
      const uint32_t v = c.getPixel(x, y);
      bool found = false;
      for (uint32_t entry : p.entries) found = found || entry == v;
      TEST_ASSERT_TRUE(found);
    }
}

template <typename Fn>
static void forEachEffect(Fn fn) {
  FadeEffect fade; PlasmaEffect plasma; TheaterChaseEffect chase;
  MovingLineEffect movingLine; BrickBreakerEffect brick; PingPongEffect pingPong;
  RadarEffect radar; CheckerboardEffect checker; FireworksEffect fireworks;
  PlasmaCloudEffect cloud; RippleEffect ripple; SnakeEffect snake;
  PacificaEffect pacifica; MatrixEffect matrix; SwirlInEffect swirlIn;
  SwirlOutEffect swirlOut; LookingEyesEffect eyes; TwinklingStarsEffect stars;
  ColorWavesEffect waves;
  RainOverlay rain; SnowOverlay snow; DrizzleOverlay drizzle; StormOverlay storm;
  ThunderOverlay thunder; FrostOverlay frost;
  IEffect* all[] = {&fade, &plasma, &chase, &movingLine, &brick, &pingPong, &radar,
                    &checker, &fireworks, &cloud, &ripple, &snake, &pacifica, &matrix, &swirlIn,
                    &swirlOut, &eyes, &stars, &waves, &rain, &snow, &drizzle, &storm, &thunder,
                    &frost};
  for (IEffect* e : all) fn(*e);
}

static void test_every_effect_is_safe_on_every_panel_size() {
  const int sizes[][2] = {{1, 8}, {4, 8}, {8, 8}, {16, 8}, {32, 8}, {37, 8}, {64, 8},
                          {128, 8}, {32, 16}, {64, 16}, {52, 16}, {64, 32}, {128, 32}};
  forEachEffect([&](IEffect& e) {
    for (const auto& sz : sizes) {
      Canvas c(sz[0], sz[1]);
      for (int64_t f : {0, 1, 2, 57, 1000, 123457}) e.render(c, f);
    }
  });
}

static void test_every_rate_stays_within_the_overflow_bound() {
  forEachEffect([](IEffect& e) {
    TEST_ASSERT_TRUE(e.rate() > 0.0f);
    TEST_ASSERT_TRUE(e.rate() <= 1.0f);
  });
}

static void test_animation_step_never_runs_backwards() {
  forEachEffect([](IEffect& e) {
    EffectSettings s;
    s.speed = 0.1f;
    s.hasSpeed = true;
    e.setSettings(s);
    long prev = e.animationStep(0);
    for (long ms = 0; ms <= 60000; ms += 137) {
      const long now = e.animationStep(ms);
      TEST_ASSERT_TRUE(now >= prev);
      prev = now;
    }
  });
}

static void test_continuous_rate_reproduces_the_base_cadence() {
  PlasmaEffect e;
  TEST_ASSERT_EQUAL_FLOAT(rate::kContinuous, e.rate());
  TEST_ASSERT_EQUAL_INT(100, static_cast<int>(e.animationStep(2400)));
}

static void test_speed_multiplies_the_declared_rate() {
  PlasmaEffect fast;
  EffectSettings s;
  s.speed = 2.0f;
  s.hasSpeed = true;
  fast.setSettings(s);
  TEST_ASSERT_EQUAL_INT(200, static_cast<int>(fast.animationStep(2400)));

  RainOverlay rain;
  const long plain = rain.animationStep(2400);
  rain.setSettings(s);
  TEST_ASSERT_EQUAL_INT(2 * plain, static_cast<int>(rain.animationStep(2400)));
}

static void test_rain_falls_at_a_watchable_pace() {
  RainOverlay rain;
  const long perSecond = rain.animationStep(1000) - rain.animationStep(0);
  TEST_ASSERT_TRUE(perSecond >= 10);
  TEST_ASSERT_TRUE(perSecond <= 20);
}

static void test_overlays_keep_their_relative_character() {
  SnowOverlay snow; DrizzleOverlay drizzle; RainOverlay rain; StormOverlay storm;
  TEST_ASSERT_TRUE(snow.rate() < drizzle.rate());
  TEST_ASSERT_TRUE(drizzle.rate() < rain.rate());
  TEST_ASSERT_TRUE(rain.rate() < storm.rate());
}

int main(int, char**) {
  UNITY_BEGIN();
  RUN_TEST(test_registry);
  RUN_TEST(test_registry_lookup_is_case_insensitive);
  RUN_TEST(test_effect_without_palette_keeps_own_colours);
  RUN_TEST(test_effect_uses_palette_when_set);
  RUN_TEST(test_plasma_animates_across_panel_sizes);
  RUN_TEST(test_plasma_workspace_growth_failure_keeps_previous_storage);
  RUN_TEST(test_blend_off_gives_hard_bands);
  RUN_TEST(test_every_rate_stays_within_the_overflow_bound);
  RUN_TEST(test_animation_step_never_runs_backwards);
  RUN_TEST(test_continuous_rate_reproduces_the_base_cadence);
  RUN_TEST(test_speed_multiplies_the_declared_rate);
  RUN_TEST(test_rain_falls_at_a_watchable_pace);
  RUN_TEST(test_overlays_keep_their_relative_character);
  RUN_TEST(test_plasma_fills);
  RUN_TEST(test_theater_chase_pattern);
  RUN_TEST(test_fade_rows_cycle_through_the_colours);
  RUN_TEST(test_brick_breaker_ball_travels_and_breaks_bricks);
  RUN_TEST(test_ping_pong_ball_crosses_the_panel);
  RUN_TEST(test_snake_hunts_the_apple_and_grows);
  RUN_TEST(test_simulations_keep_one_state_per_view);
  RUN_TEST(test_simulations_skip_a_long_absence);
  RUN_TEST(test_simulation_without_memory_draws_nothing);
  RUN_TEST(test_simulation_slots_evict_the_oldest_view_and_bound_catch_up);
  RUN_TEST(test_simulation_allocation_retries_and_keeps_its_release_function);
  RUN_TEST(test_every_effect_is_safe_on_every_panel_size);
  RUN_TEST(test_looking_eyes_scale_with_the_panel);
  RUN_TEST(test_more_effects_render_and_are_safe);
  return UNITY_END();
}
