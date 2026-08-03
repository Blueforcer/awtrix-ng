#include <cmath>
#include <vector>

#include <unity.h>

#include "core/effects/EffectRegistry.h"
#include "core/effects/effects/FadeEffect.h"
#include "core/effects/effects/MoreEffects.h"
#include "core/effects/effects/PlasmaEffect.h"
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
  Canvas c(32, 8);
  t.render(c, 0);
  TEST_ASSERT_EQUAL_HEX32(0x202020u, c.getPixel(0, 0));
  TEST_ASSERT_EQUAL_HEX32(0x000000u, c.getPixel(1, 0));
  TEST_ASSERT_EQUAL_HEX32(0x202020u, c.getPixel(3, 0));
}

static void test_fade_uniform() {
  FadeEffect f;
  Canvas c(32, 8);
  f.render(c, 0);
  const uint32_t v = c.getPixel(0, 0);
  TEST_ASSERT_EQUAL_HEX32(v, c.getPixel(31, 7));
  TEST_ASSERT_TRUE((v & 0xFF) > 0);
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
  BrickBreakerEffect{}.render(c, 1);
  PingPongEffect{}.render(c, 1);
  SnakeEffect{}.render(c, 1);
  PlasmaCloudEffect{}.render(c, 1);
  TEST_ASSERT_TRUE(true);
}

static void test_looking_eyes_centres_on_any_width() {
  auto litColumns = [](Canvas& c) {
    std::vector<int> cols;
    for (int x = 0; x < c.width(); ++x)
      for (int y = 0; y < c.height(); ++y)
        if (c.getPixel(x, y) == 0xFFFFFFu) { cols.push_back(x); break; }
    return cols;
  };
  for (int w : {32, 64, 16}) {
    Canvas c(w, 8);
    LookingEyesEffect{}.render(c, 4);
    const std::vector<int> cols = litColumns(c);
    TEST_ASSERT_TRUE_MESSAGE(!cols.empty(), "eyes drew nothing");
    const int left = cols.front(), right = cols.back();
    TEST_ASSERT_TRUE(left >= 0 && right < w);
    TEST_ASSERT_EQUAL_INT(left, w - 1 - right);
  }
  Canvas c32(32, 8);
  LookingEyesEffect{}.render(c32, 4);
  TEST_ASSERT_EQUAL_UINT32(0xFFFFFFu, c32.getPixel(8, 2));
  TEST_ASSERT_EQUAL_UINT32(0xFFFFFFu, c32.getPixel(20, 2));
}


static EffectSettings withPalette(const char* name, bool blend) {
  EffectSettings s;
  s.ramp.pal = render::paletteByName(name);
  s.ramp.blend = blend;
  return s;
}

static void test_plasma_axis_sampling_is_exact() {
  Canvas c(32, 8);
  PlasmaEffect p;
  const int64_t frame = 7;
  p.render(c, frame);

  const float t = frame * kPhasePerStep;
  for (int y = 0; y < 8; ++y) {
    for (int x = 0; x < 32; ++x) {
      const float v =
          std::sin(x * 0.3f + t) + std::sin(y * 0.3f) + std::sin((x + y) * 0.2f + t * 0.5f);
      const float u = (v + 3.0f) / 6.0f;
      int hue = static_cast<int>(u * 360.0f) % 360;
      if (hue < 0) hue += 360;
      TEST_ASSERT_EQUAL_HEX32(color::fromHsv(hue, 100, 55), c.getPixel(x, y));
    }
  }
}

static void test_plasma_palette_follows_the_same_field() {
  Canvas c(32, 8);
  PlasmaEffect p;
  p.setSettings(withPalette("Heat", true));
  const int64_t frame = 7;
  p.render(c, frame);

  const render::ColorRamp& ramp = p.settings().ramp;
  const float t = frame * kPhasePerStep;
  for (int x = 0; x < 32; x += 7) {
    const float v = std::sin(x * 0.3f + t) + std::sin(3 * 0.3f) + std::sin((x + 3) * 0.2f + t * 0.5f);
    const uint8_t idx = static_cast<uint8_t>((v + 3.0f) / 6.0f * 255.0f);
    TEST_ASSERT_EQUAL_HEX32(ramp.atIndex(idx), c.getPixel(x, 3));
  }
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
  RUN_TEST(test_plasma_axis_sampling_is_exact);
  RUN_TEST(test_plasma_palette_follows_the_same_field);
  RUN_TEST(test_blend_off_gives_hard_bands);
  RUN_TEST(test_every_rate_stays_within_the_overflow_bound);
  RUN_TEST(test_animation_step_never_runs_backwards);
  RUN_TEST(test_continuous_rate_reproduces_the_base_cadence);
  RUN_TEST(test_speed_multiplies_the_declared_rate);
  RUN_TEST(test_rain_falls_at_a_watchable_pace);
  RUN_TEST(test_overlays_keep_their_relative_character);
  RUN_TEST(test_plasma_fills);
  RUN_TEST(test_theater_chase_pattern);
  RUN_TEST(test_fade_uniform);
  RUN_TEST(test_more_effects_render_and_are_safe);
  RUN_TEST(test_looking_eyes_centres_on_any_width);
  return UNITY_END();
}
