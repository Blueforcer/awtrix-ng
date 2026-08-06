#include <unity.h>

#include "core/Settings.h"
#include "core/render/Canvas.h"
#include "core/render/Color.h"
#include "core/render/ColorGrade.h"

using namespace awtrix;
using awtrix::render::ColorGrade;
using awtrix::render::GradeParams;

void setUp() {}
void tearDown() {}

static GradeParams neutral() {
  GradeParams p;
  p.saturation = 100;
  p.gamma = 1.0f;
  p.correction = 0xFFFFFFu;
  p.tint = 0xFFFFFFu;
  return p;
}

static void test_neutral_params_are_identity() {
  ColorGrade g;
  g.setParams(neutral());
  TEST_ASSERT_TRUE(g.isIdentity());
  TEST_ASSERT_EQUAL_HEX32(0x1234ABu, g.applyPixel(0x1234ABu));
  TEST_ASSERT_EQUAL_HEX32(0xFFFFFFu, g.applyPixel(0xFFFFFFu));
}

static void test_default_params_apply_gamma() {
  ColorGrade g;
  TEST_ASSERT_FALSE(g.isIdentity());
  TEST_ASSERT_TRUE(color::red(g.applyPixel(0x808080u)) < 0x80);
  TEST_ASSERT_EQUAL_HEX32(0x000000u, g.applyPixel(0x000000u));
  TEST_ASSERT_EQUAL_HEX32(0xFFFFFFu, g.applyPixel(0xFFFFFFu));
}

static void test_gamma_is_monotonic() {
  ColorGrade g;
  GradeParams p = neutral();
  p.gamma = 2.2f;
  g.setParams(p);
  uint8_t prev = 0;
  for (int i = 0; i < 256; ++i) {
    const uint8_t v = color::red(g.applyPixel(color::pack(static_cast<uint8_t>(i), 0, 0)));
    TEST_ASSERT_TRUE(v >= prev);
    prev = v;
  }
  TEST_ASSERT_EQUAL_UINT8(255, prev);
}

// The 16-bit curve keeps a lit channel non-zero, but the step down to eight bits rounds anything
// under 129 away. So the dim end of the picture ends where the curve drops it, not where the input
// does - at the default gamma that edge sits at an input of 10.
static void test_a_dim_channel_rounds_away() {
  ColorGrade g;
  TEST_ASSERT_EQUAL_UINT8(0, color::red(g.applyPixel(color::pack(9, 0, 0))));
  TEST_ASSERT_EQUAL_UINT8(1, color::red(g.applyPixel(color::pack(10, 0, 0))));
  TEST_ASSERT_EQUAL_HEX32(0x000000u, g.applyPixel(0x000000u));
}

static void test_a_steeper_gamma_moves_that_edge_up() {
  ColorGrade shallow, steep;
  GradeParams p = neutral();
  p.gamma = 1.5f;
  shallow.setParams(p);
  p.gamma = 4.0f;
  steep.setParams(p);
  const uint32_t c = color::pack(40, 40, 40);
  TEST_ASSERT_TRUE(color::red(steep.applyPixel(c)) < color::red(shallow.applyPixel(c)));
  TEST_ASSERT_EQUAL_UINT8(0, color::red(steep.applyPixel(color::pack(1, 2, 3))));
}

// Brightness multiplies the curve's result, so it is linear in light and every setting is usable.
// Scaling before the curve instead would make white at brightness 5 work out to a sixth of a PWM
// step, leaving the first nine settings indistinguishable from off.
static void test_brightness_scales_after_the_curve() {
  ColorGrade g;
  GradeParams p;
  for (uint8_t bri : {uint8_t{1}, uint8_t{10}, uint8_t{69}, uint8_t{128}}) {
    p.brightness = bri;
    g.setParams(p);
    TEST_ASSERT_EQUAL_UINT8(bri, color::red(g.applyPixel(0xFFFFFFu)));
  }
}

// The whole point of the ordering: a colour keeps its share of white down to where the panel runs
// out of steps, rather than the control range running out first.
static void test_a_colour_keeps_its_weight_at_low_brightness() {
  ColorGrade g;
  GradeParams p;
  p.brightness = 10;
  g.setParams(p);
  TEST_ASSERT_EQUAL_UINT8(2, color::red(g.applyPixel(0x666666u)));
  p.brightness = 3;
  g.setParams(p);
  TEST_ASSERT_EQUAL_UINT8(1, color::red(g.applyPixel(0x666666u)));
}

static void test_full_brightness_leaves_white_alone() {
  ColorGrade full, almost;
  GradeParams p;
  full.setParams(p);
  p.brightness = 254;
  almost.setParams(p);
  TEST_ASSERT_EQUAL_UINT8(255, color::red(full.applyPixel(0xFFFFFFu)));
  TEST_ASSERT_TRUE(color::red(almost.applyPixel(0xFFFFFFu)) < 255);
}

static void test_brightness_zero_blanks_the_panel() {
  ColorGrade g;
  GradeParams p;
  p.brightness = 0;
  g.setParams(p);
  TEST_ASSERT_EQUAL_HEX32(0x000000u, g.applyPixel(0xFFFFFFu));
}

static void test_lit_channel_floor_does_not_override_correction() {
  ColorGrade g;
  GradeParams p = neutral();
  p.gamma = 4.0f;
  p.correction = 0xFF0000u;
  g.setParams(p);
  const uint32_t c = g.applyPixel(0x0000FFu);
  TEST_ASSERT_EQUAL_UINT8(0, color::green(c));
  TEST_ASSERT_EQUAL_UINT8(0, color::blue(c));
}

static void test_saturation_zero_is_grey() {
  ColorGrade g;
  GradeParams p = neutral();
  p.saturation = 0;
  g.setParams(p);
  TEST_ASSERT_FALSE(g.isIdentity());
  const uint32_t c = g.applyPixel(0xFF0000u);
  TEST_ASSERT_EQUAL_HEX32(0x4C4C4Cu, c);
}

// The channel balance compensates each die's efficiency, so it scales light and not the encoded
// value - a half-strength correction has to stay half-strength whatever the gamma is.
static void test_correction_is_independent_of_gamma() {
  ColorGrade g;
  GradeParams p = neutral();
  p.correction = 0x808080u;
  for (float gamma : {1.0f, 1.9f, 2.8f}) {
    p.gamma = gamma;
    g.setParams(p);
    TEST_ASSERT_EQUAL_UINT8(128, color::red(g.applyPixel(0xFFFFFFu)));
  }
}

static void test_correction_scales_channels() {
  ColorGrade g;
  GradeParams p = neutral();
  p.correction = 0xFF0000u;
  g.setParams(p);
  const uint32_t c = g.applyPixel(0xFFFFFFu);
  TEST_ASSERT_EQUAL_UINT8(255, color::red(c));
  TEST_ASSERT_EQUAL_UINT8(0, color::green(c));
  TEST_ASSERT_EQUAL_UINT8(0, color::blue(c));
}

static void test_correction_and_tint_compose() {
  ColorGrade both, single;
  GradeParams p = neutral();
  p.correction = 0x808080u;
  p.tint = 0x808080u;
  both.setParams(p);

  GradeParams q = neutral();
  q.correction = 0x808080u;
  single.setParams(q);

  TEST_ASSERT_TRUE(color::red(both.applyPixel(0xFFFFFFu)) <
                   color::red(single.applyPixel(0xFFFFFFu)));
  TEST_ASSERT_EQUAL_UINT8(color::scale8(color::scale8(255, 128), 128),
                          color::red(both.applyPixel(0xFFFFFFu)));
}

static void test_saturation_runs_before_the_channel_stages() {
  ColorGrade g;
  GradeParams p = neutral();
  p.saturation = 0;
  p.correction = 0xFF0000u;
  g.setParams(p);
  const uint32_t c = g.applyPixel(0x00FF00u);
  TEST_ASSERT_EQUAL_UINT8(0x95, color::red(c));
  TEST_ASSERT_EQUAL_UINT8(0, color::green(c));
  TEST_ASSERT_EQUAL_UINT8(0, color::blue(c));
}

static void test_zero_gamma_falls_back_to_linear() {
  ColorGrade g;
  GradeParams p = neutral();
  p.gamma = 0.0f;
  g.setParams(p);
  TEST_ASSERT_TRUE(g.isIdentity());
}

static void test_params_are_kept() {
  ColorGrade g;
  GradeParams p = neutral();
  p.saturation = 42;
  g.setParams(p);
  TEST_ASSERT_EQUAL_INT(42, g.params().saturation);
  g.setParams(p);
  TEST_ASSERT_EQUAL_INT(42, g.params().saturation);
}

static void test_apply_grades_whole_canvas() {
  Canvas src(4, 2), dst(4, 2);
  src.clear(0xFF0000u);
  src.setPixel(0, 0, 0x00FF00u);

  ColorGrade g;
  GradeParams p = neutral();
  p.saturation = 0;
  g.setParams(p);
  g.apply(src, dst);

  TEST_ASSERT_EQUAL_HEX32(0x959595u, dst.getPixel(0, 0));
  TEST_ASSERT_EQUAL_HEX32(0x4C4C4Cu, dst.getPixel(1, 0));
  TEST_ASSERT_EQUAL_HEX32(0x4C4C4Cu, dst.getPixel(3, 1));
}

static void test_apply_identity_copies() {
  Canvas src(4, 2), dst(4, 2);
  src.clear(0x123456u);
  dst.clear(0xFFFFFFu);

  ColorGrade g;
  g.setParams(neutral());
  g.apply(src, dst);

  TEST_ASSERT_EQUAL_HEX32(0x123456u, dst.getPixel(2, 1));
}

static void test_apply_ignores_size_mismatch() {
  Canvas src(4, 2), dst(8, 2);
  src.clear(0xFF0000u);
  dst.clear(0x000000u);

  ColorGrade g;
  GradeParams p = neutral();
  p.saturation = 0;
  g.setParams(p);
  g.apply(src, dst);

  TEST_ASSERT_EQUAL_HEX32(0x000000u, dst.getPixel(0, 0));
}

static void test_gradeFrom_settings() {
  Settings s;
  s.saturation = 30;
  s.gamma = 2.0f;
  const GradeParams p = render::gradeFrom(s);
  TEST_ASSERT_EQUAL_INT(30, p.saturation);
  TEST_ASSERT_EQUAL_HEX32(0xFFFFFFu, p.correction);
  TEST_ASSERT_EQUAL_HEX32(0xFFFFFFu, p.tint);

  s.colorTint = OptColor{0x804020u, true};
  TEST_ASSERT_EQUAL_HEX32(0x804020u, render::gradeFrom(s).tint);
}

int main(int, char**) {
  UNITY_BEGIN();
  RUN_TEST(test_neutral_params_are_identity);
  RUN_TEST(test_default_params_apply_gamma);
  RUN_TEST(test_gamma_is_monotonic);
  RUN_TEST(test_a_dim_channel_rounds_away);
  RUN_TEST(test_a_steeper_gamma_moves_that_edge_up);
  RUN_TEST(test_brightness_scales_after_the_curve);
  RUN_TEST(test_a_colour_keeps_its_weight_at_low_brightness);
  RUN_TEST(test_full_brightness_leaves_white_alone);
  RUN_TEST(test_brightness_zero_blanks_the_panel);
  RUN_TEST(test_lit_channel_floor_does_not_override_correction);
  RUN_TEST(test_saturation_zero_is_grey);
  RUN_TEST(test_correction_is_independent_of_gamma);
  RUN_TEST(test_correction_scales_channels);
  RUN_TEST(test_correction_and_tint_compose);
  RUN_TEST(test_saturation_runs_before_the_channel_stages);
  RUN_TEST(test_zero_gamma_falls_back_to_linear);
  RUN_TEST(test_params_are_kept);
  RUN_TEST(test_apply_grades_whole_canvas);
  RUN_TEST(test_apply_identity_copies);
  RUN_TEST(test_apply_ignores_size_mismatch);
  RUN_TEST(test_gradeFrom_settings);
  return UNITY_END();
}
