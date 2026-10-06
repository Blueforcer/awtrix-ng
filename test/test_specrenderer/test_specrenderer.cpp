#include <unity.h>
#include "../Visuals.h"

#include "core/apps/SpecRenderer.h"
#include "core/payload/PayloadParser.h"
#include "core/render/PaletteStore.h"

using namespace awtrix;

static const FontGlyph kG[] = {{0, 3, 3, 4, 0, 0}};
static const uint8_t kB[] = {0xFF, 0x80};
static const GfxFont kFont = {kB, kG, 'A', 'A', 8};

static render::SpecRender rc() { return render::SpecRender{}; }

static const render::ResolvedScroll& animatingScroll() {
  static const render::ResolvedScroll rs = [] {
    ScrollDefaults d;
    render::ScrollLayout l;
    l.text.advance = 60;
    l.text.inkRight = 58;
    l.availWidth = 32;
    l.canvasWidth = 32;
    return render::resolve(ScrollSpec{}, d, l);
  }();
  return rs;
}

void setUp() {}
void tearDown() {}

static void test_background_fill() {
  AppSpec s;
  s.hasBackgroundColor = true;
  s.backgroundColor = 0x112233u;
  Canvas c(32, 8);
  render::renderSpec(c, s, kFont, rc());
  TEST_ASSERT_EQUAL_HEX32(0x112233u, c.getPixel(0, 0));
  TEST_ASSERT_EQUAL_HEX32(0x112233u, c.getPixel(31, 6));
}

static int countOf(const Canvas& c, uint32_t color) {
  int n = 0;
  for (int y = 0; y < c.height(); ++y)
    for (int x = 0; x < c.width(); ++x) n += c.getPixel(x, y) == color;
  return n;
}

static void test_progress_bar() {
  int filled[2], track[2];
  const int progress[2] = {25, 75};
  for (int i = 0; i < 2; ++i) {
    AppSpec s;
    s.extrasMut().progress = progress[i];
    s.extrasMut().progressColor = 0xFF0000u;
    s.extrasMut().progressTrackColor = 0x0000FFu;
    Canvas c(32, 8);
    render::renderSpec(c, s, kFont, rc());
    filled[i] = countOf(c, 0xFF0000u);
    track[i] = countOf(c, 0x0000FFu);
  }
  TEST_ASSERT_TRUE(filled[0] > 0 && track[1] > 0);
  TEST_ASSERT_TRUE(filled[1] > filled[0]);
  TEST_ASSERT_EQUAL_INT(filled[0] + track[0], filled[1] + track[1]);
}

static void test_draw_op_overlays() {
  AppSpec s;
  TEST_ASSERT_TRUE(payload::parse(R"({"draw":[["pixel",2,2,"#FF00FF"]]})", false, s));
  Canvas c(32, 8);
  render::renderSpec(c, s, kFont, rc());
  TEST_ASSERT_EQUAL_HEX32(0xFF00FFu, c.getPixel(2, 2));
}

static void test_bar_chart() {
  int lit[2];
  const int value[2] = {4, 8};
  for (int i = 0; i < 2; ++i) {
    AppSpec s;
    s.extrasMut().barChart = {value[i], 8};
    s.hasTextColor = true;
    s.textColor = 0x00FF00u;
    Canvas c(32, 8);
    render::renderSpec(c, s, kFont, rc());
    lit[i] = countOf(c, 0x00FF00u);
  }
  TEST_ASSERT_TRUE(lit[0] > 0 && lit[1] > lit[0]);
}

static bool anyLit(Canvas& c) {
  for (int y = 0; y < c.height(); ++y)
    for (int x = 0; x < c.width(); ++x)
      if (c.getPixel(x, y) != 0) return true;
  return false;
}

static void test_uppercase_textcase() {
  AppSpec s; s.text = "a"; s.textCase = TextCase::Upper;
  Canvas c(32, 8);
  render::renderSpec(c, s, kFont, rc());
  TEST_ASSERT_TRUE(test::countPixels(c, test::lit) > 0);
  AppSpec s2; s2.text = "a"; s2.textCase = TextCase::AsTyped;
  Canvas c2(32, 8);
  { auto r = rc(); r.uppercase = true; render::renderSpec(c2, s2, kFont, r); }
  TEST_ASSERT_FALSE(anyLit(c2));
}

static void paintFromPalette(AppSpec& s, const uint32_t* stops, std::size_t n) {
  AppSpecExtras& x = s.extrasMut();
  x.palette.pal = render::paletteFromStopList(stops, n);
  x.textUsesPalette = true;
}

static void test_palette_text_starts_at_the_first_stop() {
  const uint32_t stops[2] = {0xFF0000u, 0x0000FFu};
  AppSpec s; s.text = "A";
  paintFromPalette(s, stops, 2);
  Canvas c(32, 8);
  render::renderSpec(c, s, kFont, rc());
  TEST_ASSERT_TRUE(test::countPixels(c, [](uint32_t p) { return p == 0xFF0000u; }) > 0);
}

static void test_palette_outranks_fragment_colours() {
  const uint32_t stops[2] = {0xFF0000u, 0x0000FFu};
  AppSpec s;
  TEST_ASSERT_TRUE(payload::parse(R"({"text":[{"text":"A","color":"#00FF00"}]})", false, s));
  paintFromPalette(s, stops, 2);
  Canvas c(32, 8);
  render::renderSpec(c, s, kFont, rc());
  TEST_ASSERT_TRUE(test::countPixels(c, [](uint32_t p) { return p == 0xFF0000u; }) > 0);
}

static const uint16_t kWideIndex[] = {1};
static const FontRange kWideRanges[] = {{0x100, 0x100, kWideIndex}};
static const GfxFont kWideFont = {kB, kG, 'A', 'A', 8, kWideRanges, 1};

static void test_fragment_colours_are_counted_per_glyph() {
  AppSpec s;
  TEST_ASSERT_TRUE(payload::parse(
      "{\"text\":[{\"text\":\"\xC4\x80\",\"color\":\"#FF0000\"},{\"text\":\"A\",\"color\":\"#0000FF\"}]}",
      false, s));
  s.textAlign = Align::Start;
  Canvas c(32, 8);
  render::renderSpec(c, s, kWideFont, rc());

  TEST_ASSERT_EQUAL_HEX32(0xFF0000u, c.getPixel(0, 6));
  TEST_ASSERT_EQUAL_HEX32(0x0000FFu, c.getPixel(4, 6));
}

static void test_end_aligned_text_ends_at_the_right_edge() {
  AppSpec s; s.text = "A"; s.textAlign = Align::End;
  Canvas c(32, 8);
  render::renderSpec(c, s, kFont, rc());
  const auto box = test::bounds(c, test::lit);
  TEST_ASSERT_TRUE(test::countPixels(c, test::lit) > 0);
  TEST_ASSERT_EQUAL_INT(c.width() - 1, box.right);
  Canvas wide(64, 8);
  render::renderSpec(wide, s, kFont, rc());
  TEST_ASSERT_TRUE(test::countPixels(wide, test::lit) > 0);
  TEST_ASSERT_EQUAL_INT(wide.width() - 1, test::bounds(wide, test::lit).right);
}

static void test_palette_paint_without_a_palette_falls_back() {
  AppSpec s; s.text = "A"; s.hasTextColor = true; s.textColor = 0x00FF00u;
  s.extrasMut().textUsesPalette = true;
  Canvas c(32, 8);
  render::renderSpec(c, s, kFont, rc());
  TEST_ASSERT_TRUE(test::countPixels(c, [](uint32_t p) { return p == 0x00FF00u; }) > 0);
}

static void test_chart_paints_from_the_palette() {
  const uint32_t stops[2] = {0xFF0000u, 0x0000FFu};
  AppSpec s;
  AppSpecExtras& x = s.extrasMut();
  x.barChart = {4, 8};
  x.palette.pal = render::paletteFromStopList(stops, 2);
  x.chartUsesPalette = true;
  Canvas c(32, 8);
  render::renderSpec(c, s, kFont, rc());
  TEST_ASSERT_EQUAL_HEX32(0x0000FFu, c.getPixel(16, 0));
  TEST_ASSERT_TRUE(c.getPixel(0, 7) != 0x0000FFu);
}

static void test_pixels_are_drawn() {
  AppSpec s;
  TEST_ASSERT_TRUE(payload::parse(R"({"draw":[["pixels","#00FF00",0,0,5,3,31,7]]})", false, s));
  Canvas c(32, 8);
  render::renderSpec(c, s, kFont, rc());
  TEST_ASSERT_EQUAL_HEX32(0x00FF00u, c.getPixel(0, 0));
  TEST_ASSERT_EQUAL_HEX32(0x00FF00u, c.getPixel(5, 3));
  TEST_ASSERT_EQUAL_HEX32(0x00FF00u, c.getPixel(31, 7));
}

static void test_draw_op_without_a_color_uses_the_text_color() {
  AppSpec s;
  TEST_ASSERT_TRUE(payload::parse(R"({"draw":[["pixel",4,4]]})", false, s));
  s.hasTextColor = true;
  s.textColor = 0xFF00FFu;
  Canvas c(32, 8);
  render::renderSpec(c, s, kFont, rc());
  TEST_ASSERT_EQUAL_HEX32(0xFF00FFu, c.getPixel(4, 4));
}

static void test_chart_color_applies_to_the_line_chart() {
  AppSpec s;
  AppSpecExtras& x = s.extrasMut();
  x.lineChart = {0, 10};
  x.hasChartColor = true;
  x.chartColor = 0xFF00FFu;
  s.hasTextColor = true;
  s.textColor = 0x0000FFu;
  Canvas c(32, 8);
  render::renderSpec(c, s, kFont, rc());
  TEST_ASSERT_EQUAL_HEX32(0xFF00FFu, c.getPixel(0, 7));
}

static void test_line_chart_without_chart_color_uses_text_color() {
  AppSpec s;
  AppSpecExtras& x = s.extrasMut();
  x.lineChart = {0, 10};
  s.hasTextColor = true;
  s.textColor = 0x0000FFu;
  Canvas c(32, 8);
  render::renderSpec(c, s, kFont, rc());
  TEST_ASSERT_EQUAL_HEX32(0x0000FFu, c.getPixel(0, 7));
}

static void test_overflowing_text_drawn_at_textX() {
  AppSpec s;
  s.text = "AAAAAAAAAA";
  s.textAlign = Align::Center;
  Canvas c(32, 8);
  auto r = rc();
  r.textX = 5;
  r.scroll = &animatingScroll();
  render::renderSpec(c, s, kFont, r);
  TEST_ASSERT_TRUE(c.getPixel(5, 6) != 0);
  TEST_ASSERT_EQUAL_HEX32(0u, c.getPixel(1, 6));
}

static const render::ResolvedScroll& loopingScroll() {
  static const render::ResolvedScroll rs = [] {
    ScrollDefaults d;
    d.mode = ScrollMode::Loop;
    d.gap = 4;
    d.whenFits = ScrollWhenFits::Scroll;
    render::ScrollLayout l;
    l.text.advance = 8;
    l.text.inkRight = 6;
    l.availWidth = 32;
    l.canvasWidth = 32;
    return render::resolve(ScrollSpec{}, d, l);
  }();
  return rs;
}

static void test_loop_fills_the_seam_with_neighbouring_copies() {
  AppSpec s;
  s.text = "AA";
  Canvas c(32, 8);
  auto r = rc();
  r.textX = 0;
  r.scroll = &loopingScroll();
  render::renderSpec(c, s, kFont, r);

  TEST_ASSERT_TRUE_MESSAGE(c.getPixel(0, 6) != 0, "the copy at textX");
  TEST_ASSERT_TRUE_MESSAGE(c.getPixel(12, 6) != 0, "the copy one period to the right");
  TEST_ASSERT_TRUE_MESSAGE(c.getPixel(24, 6) != 0, "and the one after that");
}

static void test_a_wrapping_marquee_draws_only_one_copy() {
  AppSpec s;
  s.text = "AA";
  Canvas c(32, 8);
  auto r = rc();
  r.textX = 0;
  r.scroll = &animatingScroll();
  render::renderSpec(c, s, kFont, r);

  TEST_ASSERT_TRUE(c.getPixel(0, 6) != 0);
  TEST_ASSERT_EQUAL_HEX32_MESSAGE(0u, c.getPixel(12, 6), "only loop repeats the text");
}

static void test_fitting_text_ignores_textX() {
  AppSpec s;
  s.text = "A";
  Canvas c(32, 8);
  auto r = rc();
  r.textX = 5;
  render::renderSpec(c, s, kFont, r);
  TEST_ASSERT_TRUE(c.getPixel(14, 6) != 0);
}

static bool anyPartial(Canvas& c) {
  for (int y = 0; y < c.height(); ++y)
    for (int x = 0; x < c.width(); ++x) {
      const uint32_t p = c.getPixel(x, y);
      if (p != 0u && p != 0xFFFFFFu) return true;
    }
  return false;
}

static void test_a_fractional_scroll_position_snaps_to_whole_pixels() {
  AppSpec s;
  s.text = "AAAAAAAAAA";
  Canvas c(32, 8);
  auto r = rc();
  r.textX = 5.5f;
  r.scroll = &animatingScroll();
  render::renderSpec(c, s, kFont, r);
  TEST_ASSERT_TRUE(c.getPixel(5, 6) != 0);
  TEST_ASSERT_FALSE_MESSAGE(anyPartial(c), "scrolling text must land on whole pixels");
}

static void test_toptext_zorder() {
  AppSpec s;
  s.text = "A";
  s.hasTextColor = true;
  s.textColor = 0xFF0000u;
  s.extrasMut().barChart = {8};
  s.extrasMut().hasChartColor = true;
  s.extrasMut().chartColor = 0x0000FFu;

  Canvas under(32, 8);
  render::renderSpec(under, s, kFont, rc());
  TEST_ASSERT_EQUAL_HEX32(0x0000FFu, under.getPixel(14, 6));

  s.textInFront = true;
  Canvas over(32, 8);
  render::renderSpec(over, s, kFont, rc());
  TEST_ASSERT_EQUAL_HEX32(0xFF0000u, over.getPixel(14, 6));
}

static void test_blink_wall_time() {
  AppSpec s;
  s.text = "A";
  s.hasTextColor = true;
  s.textColor = 0xFF0000u;
  s.textBlinkMs = 1000;
  Canvas on(32, 8);
  { auto r = rc(); r.nowMs = 600; render::renderSpec(on, s, kFont, r); }
  TEST_ASSERT_EQUAL_HEX32(0xFF0000u, on.getPixel(14, 6));
  Canvas off(32, 8);
  { auto r = rc(); r.nowMs = 100; render::renderSpec(off, s, kFont, r); }
  TEST_ASSERT_EQUAL_HEX32(0x000000u, off.getPixel(14, 6));
}

static void test_bars_respect_icon_width() {
  AppSpec s;
  s.extrasMut().barChart = {8, 8};
  s.hasTextColor = true;
  s.textColor = 0x00FF00u;
  Canvas c(32, 8);
  { auto r = rc(); r.iconWidth = 9; render::renderSpec(c, s, kFont, r); }
  TEST_ASSERT_EQUAL_HEX32(0u, c.getPixel(0, 7));
  TEST_ASSERT_EQUAL_HEX32(0x00FF00u, c.getPixel(9, 7));
}

static void test_bars_with_negative_values_stay_on_canvas() {
  AppSpec s;
  s.extrasMut().barChart = {-4, 4};
  s.hasTextColor = true;
  s.textColor = 0x00FF00u;
  constexpr int pixels = 32 * 8;
  uint32_t guarded[pixels + 2] = {};
  guarded[0] = guarded[pixels + 1] = 0xDEADBEEFu;
  Canvas c(32, 8, guarded + 1);
  render::renderSpec(c, s, kFont, rc());
  TEST_ASSERT_TRUE(countOf(c, 0x00FF00u) > 0);
  TEST_ASSERT_EQUAL_HEX32(0xDEADBEEFu, guarded[0]);
  TEST_ASSERT_EQUAL_HEX32(0xDEADBEEFu, guarded[pixels + 1]);
}

static void test_line_chart_with_negative_values_stays_on_canvas() {
  AppSpec s;
  s.extrasMut().lineChart = {-4, 4};
  s.hasTextColor = true;
  s.textColor = 0x00FF00u;
  Canvas c(32, 8);
  { auto r = rc(); render::renderSpec(c, s, kFont, r); }
  TEST_ASSERT_EQUAL_HEX32(0x00FF00u, c.getPixel(0, 7));
  TEST_ASSERT_EQUAL_HEX32(0x00FF00u, c.getPixel(31, 0));
}

int main(int, char**) {
  UNITY_BEGIN();
  RUN_TEST(test_pixels_are_drawn);
  RUN_TEST(test_draw_op_without_a_color_uses_the_text_color);
  RUN_TEST(test_chart_color_applies_to_the_line_chart);
  RUN_TEST(test_line_chart_without_chart_color_uses_text_color);
  RUN_TEST(test_bars_with_negative_values_stay_on_canvas);
  RUN_TEST(test_line_chart_with_negative_values_stays_on_canvas);
  RUN_TEST(test_background_fill);
  RUN_TEST(test_toptext_zorder);
  RUN_TEST(test_blink_wall_time);
  RUN_TEST(test_bars_respect_icon_width);
  RUN_TEST(test_overflowing_text_drawn_at_textX);
  RUN_TEST(test_loop_fills_the_seam_with_neighbouring_copies);
  RUN_TEST(test_a_wrapping_marquee_draws_only_one_copy);
  RUN_TEST(test_fitting_text_ignores_textX);
  RUN_TEST(test_a_fractional_scroll_position_snaps_to_whole_pixels);
  RUN_TEST(test_uppercase_textcase);
  RUN_TEST(test_palette_text_starts_at_the_first_stop);
  RUN_TEST(test_palette_outranks_fragment_colours);
  RUN_TEST(test_fragment_colours_are_counted_per_glyph);
  RUN_TEST(test_palette_paint_without_a_palette_falls_back);
  RUN_TEST(test_end_aligned_text_ends_at_the_right_edge);
  RUN_TEST(test_chart_paints_from_the_palette);
  RUN_TEST(test_progress_bar);
  RUN_TEST(test_draw_op_overlays);
  RUN_TEST(test_bar_chart);
  return UNITY_END();
}
