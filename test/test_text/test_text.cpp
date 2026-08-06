#include <unity.h>

#include <memory>

#include "core/render/Color.h"
#include "core/render/TextRenderer.h"

using namespace awtrix;

static const FontGlyph kGlyphs[] = {
    {0, 3, 3, 4, 0, 0},
    {2, 2, 2, 3, 0, 0},
    {3, 2, 2, 2, 0, 0},
};
static const uint8_t kBitmap[] = {0xFF, 0x80, 0x90, 0x00};
static const GfxFont kFont = {kBitmap, kGlyphs, 'A', 'C', 8};

void setUp() {}
void tearDown() {}

static void test_char_advance() {
  TEST_ASSERT_EQUAL_INT(4, text::charAdvance(kFont, 'A'));
  TEST_ASSERT_EQUAL_INT(3, text::charAdvance(kFont, 'B'));
  TEST_ASSERT_EQUAL_INT(0, text::charAdvance(kFont, 'Z'));
}

static void test_width() {
  TEST_ASSERT_EQUAL_INT(7, text::width(kFont, "AB"));
  TEST_ASSERT_EQUAL_INT(0, text::width(kFont, ""));
}

static void test_measure_separates_advance_from_ink() {
  const text::TextMetrics a = text::measure(kFont, "A");
  TEST_ASSERT_EQUAL_INT(4, a.advance);
  TEST_ASSERT_EQUAL_INT(0, a.inkLeft);
  TEST_ASSERT_EQUAL_INT(2, a.inkRight);
  TEST_ASSERT_EQUAL_INT(3, a.inkWidth());

  const text::TextMetrics ab = text::measure(kFont, "AB");
  TEST_ASSERT_EQUAL_INT(7, ab.advance);
  TEST_ASSERT_EQUAL_INT(5, ab.inkRight);
  TEST_ASSERT_EQUAL_INT(6, ab.inkWidth());
}

static void test_measure_ignores_a_blank_glyph_at_either_end() {
  const text::TextMetrics trailing = text::measure(kFont, "AC");
  TEST_ASSERT_EQUAL_INT(6, trailing.advance);
  TEST_ASSERT_EQUAL_INT(0, trailing.inkLeft);
  TEST_ASSERT_EQUAL_INT(2, trailing.inkRight);
  TEST_ASSERT_EQUAL_INT(3, trailing.inkWidth());

  const text::TextMetrics leading = text::measure(kFont, "CA");
  TEST_ASSERT_EQUAL_INT(6, leading.advance);
  TEST_ASSERT_EQUAL_INT(2, leading.inkLeft);
  TEST_ASSERT_EQUAL_INT(4, leading.inkRight);
  TEST_ASSERT_EQUAL_INT(3, leading.inkWidth());
}

static void test_measure_of_empty_text_has_no_ink() {
  const text::TextMetrics m = text::measure(kFont, "");
  TEST_ASSERT_EQUAL_INT(0, m.advance);
  TEST_ASSERT_FALSE(m.hasInk());
  TEST_ASSERT_EQUAL_INT(0, m.inkWidth());
}

static void test_centering_places_the_ink_not_the_advance() {
  Canvas c(8, 8);
  TEST_ASSERT_EQUAL_INT(2, text::drawCentered(c, kFont, "AC", 0, 0xFF0000u));
  TEST_ASSERT_EQUAL_INT(2, text::drawCentered(c, kFont, "A", 0, 0xFF0000u));
}

static void test_centering_skips_a_leading_blank() {
  Canvas c(8, 8);
  const int x = text::drawCentered(c, kFont, "CA", 0, 0xFF0000u);
  TEST_ASSERT_EQUAL_INT(0, x);
  TEST_ASSERT_EQUAL_HEX32(0xFF0000u, c.getPixel(2, 0));
}

static void test_draw_solid_A() {
  Canvas c(8, 8);
  int adv = text::drawText(c, kFont, 0, 0, "A", 0xFF0000u);
  TEST_ASSERT_EQUAL_INT(4, adv);
  for (int y = 0; y < 3; ++y)
    for (int x = 0; x < 3; ++x) TEST_ASSERT_EQUAL_HEX32(0xFF0000u, c.getPixel(x, y));
  TEST_ASSERT_EQUAL_HEX32(0u, c.getPixel(3, 0));
}

static void test_draw_B_diagonal() {
  Canvas c(8, 8);
  text::drawChar(c, kFont, 0, 0, 'B', 0x00FF00u);
  TEST_ASSERT_EQUAL_HEX32(0x00FF00u, c.getPixel(0, 0));
  TEST_ASSERT_EQUAL_HEX32(0u, c.getPixel(1, 0));
  TEST_ASSERT_EQUAL_HEX32(0u, c.getPixel(0, 1));
  TEST_ASSERT_EQUAL_HEX32(0x00FF00u, c.getPixel(1, 1));
}

static void test_draw_AB_positions() {
  Canvas c(8, 8);
  text::drawText(c, kFont, 0, 0, "AB", 0xFFFFFFu);
  TEST_ASSERT_EQUAL_HEX32(0xFFFFFFu, c.getPixel(2, 2));
  TEST_ASSERT_EQUAL_HEX32(0xFFFFFFu, c.getPixel(4, 0));
  TEST_ASSERT_EQUAL_HEX32(0u, c.getPixel(5, 0));
  TEST_ASSERT_EQUAL_HEX32(0xFFFFFFu, c.getPixel(5, 1));
}

static void test_offcanvas_is_safe() {
  Canvas c(8, 8);
  text::drawText(c, kFont, 30, 0, "AB", 0xFFFFFFu);
  TEST_ASSERT_TRUE(true);
}

static void test_run_lands_on_the_pixel_grid() {
  Canvas c(8, 8);
  text::TextPaint paint;
  paint.flat = 0xFFFFFFu;
  const int adv = text::drawRun(c, kFont, 2, 0, "A", paint);
  TEST_ASSERT_EQUAL_INT(4, adv);
  for (int y = 0; y < 3; ++y)
    for (int x = 2; x < 5; ++x) TEST_ASSERT_EQUAL_HEX32(0xFFFFFFu, c.getPixel(x, y));
  TEST_ASSERT_EQUAL_HEX32(0u, c.getPixel(1, 0));
  TEST_ASSERT_EQUAL_HEX32(0u, c.getPixel(5, 0));
}

static void test_run_covers_the_background() {
  Canvas c(8, 8);
  c.clear(0x00FF00u);
  text::TextPaint paint;
  paint.flat = 0xFF0000u;
  text::drawRun(c, kFont, 2, 0, "A", paint);
  TEST_ASSERT_EQUAL_HEX32(0xFF0000u, c.getPixel(2, 0));
  TEST_ASSERT_EQUAL_HEX32(0x00FF00u, c.getPixel(1, 0));
}


static render::ColorRamp rampOf(const uint32_t* stops, std::size_t n) {
  render::ColorRamp r;
  r.pal = std::make_shared<const render::Palette>(render::paletteFromStops(stops, n));
  return r;
}

static text::TextPaint paintWith(const render::ColorRamp& r) {
  text::TextPaint p;
  p.ramp = &r;
  return p;
}

static void test_ramp_stretches_between_the_ink() {
  const uint32_t stops[2] = {0xFF0000u, 0x0000FFu};
  const render::ColorRamp r = rampOf(stops, 2);
  Canvas c(16, 8);
  text::drawRun(c, kFont, 0, 0, "AA", paintWith(r));
  TEST_ASSERT_EQUAL_HEX32(0xFF0000u, c.getPixel(0, 0));
  TEST_ASSERT_EQUAL_HEX32(0x0000FFu, c.getPixel(6, 0));
}

static void test_ramp_varies_within_one_glyph() {
  const uint32_t stops[2] = {0xFF0000u, 0x0000FFu};
  const render::ColorRamp r = rampOf(stops, 2);
  Canvas c(16, 8);
  text::drawRun(c, kFont, 0, 0, "A", paintWith(r));
  TEST_ASSERT_NOT_EQUAL(c.getPixel(0, 0), c.getPixel(1, 0));
  TEST_ASSERT_NOT_EQUAL(c.getPixel(1, 0), c.getPixel(2, 0));
}

static void test_ramp_span_repeats() {
  const uint32_t stops[2] = {0xFF0000u, 0x0000FFu};
  render::ColorRamp r = rampOf(stops, 2);
  r.spanPx = 4;
  Canvas c(16, 8);
  text::drawRun(c, kFont, 0, 0, "AAA", paintWith(r));
  TEST_ASSERT_EQUAL_HEX32(c.getPixel(0, 0), c.getPixel(4, 0));
  TEST_ASSERT_EQUAL_HEX32(c.getPixel(1, 0), c.getPixel(5, 0));
}

static void test_ramp_origin_shifts_by_whole_spans() {
  const uint32_t stops[2] = {0xFF0000u, 0x0000FFu};
  render::ColorRamp r = rampOf(stops, 2);
  r.spanPx = 4;

  Canvas a(16, 8);
  text::drawRun(a, kFont, 0, 0, "AA", paintWith(r));

  text::TextPaint shifted = paintWith(r);
  shifted.rampOriginPx = 1;
  Canvas b(16, 8);
  text::drawRun(b, kFont, 0, 0, "AA", shifted);
  TEST_ASSERT_NOT_EQUAL(a.getPixel(0, 0), b.getPixel(0, 0));

  text::TextPaint wrapped = paintWith(r);
  wrapped.rampOriginPx = 4;
  Canvas d(16, 8);
  text::drawRun(d, kFont, 0, 0, "AA", wrapped);
  TEST_ASSERT_EQUAL_HEX32(a.getPixel(0, 0), d.getPixel(0, 0));
}

static void test_ramp_origin_handles_negatives() {
  const uint32_t stops[2] = {0xFF0000u, 0x0000FFu};
  render::ColorRamp r = rampOf(stops, 2);
  r.spanPx = 4;

  text::TextPaint a = paintWith(r);
  a.rampOriginPx = -4;
  Canvas ca(16, 8);
  text::drawRun(ca, kFont, 0, 0, "AA", a);

  Canvas cb(16, 8);
  text::drawRun(cb, kFont, 0, 0, "AA", paintWith(r));
  TEST_ASSERT_EQUAL_HEX32(cb.getPixel(0, 0), ca.getPixel(0, 0));
}

static void test_glyph_colours_paint_per_glyph() {
  const uint32_t cols[2] = {0xFF0000u, 0x00FF00u};
  text::TextPaint p;
  p.glyphColors = cols;
  p.glyphCount = 2;
  Canvas c(16, 8);
  text::drawRun(c, kFont, 0, 0, "AA", p);
  TEST_ASSERT_EQUAL_HEX32(0xFF0000u, c.getPixel(0, 0));
  TEST_ASSERT_EQUAL_HEX32(0x00FF00u, c.getPixel(4, 0));
}

static void test_empty_ramp_falls_back_to_flat() {
  render::ColorRamp r;
  text::TextPaint p = paintWith(r);
  p.flat = 0x00FF00u;
  Canvas c(16, 8);
  text::drawRun(c, kFont, 0, 0, "A", p);
  TEST_ASSERT_EQUAL_HEX32(0x00FF00u, c.getPixel(0, 0));
}

int main(int, char**) {
  UNITY_BEGIN();
  RUN_TEST(test_char_advance);
  RUN_TEST(test_width);
  RUN_TEST(test_measure_separates_advance_from_ink);
  RUN_TEST(test_measure_ignores_a_blank_glyph_at_either_end);
  RUN_TEST(test_measure_of_empty_text_has_no_ink);
  RUN_TEST(test_centering_places_the_ink_not_the_advance);
  RUN_TEST(test_centering_skips_a_leading_blank);
  RUN_TEST(test_draw_solid_A);
  RUN_TEST(test_draw_B_diagonal);
  RUN_TEST(test_draw_AB_positions);
  RUN_TEST(test_offcanvas_is_safe);
  RUN_TEST(test_run_lands_on_the_pixel_grid);
  RUN_TEST(test_run_covers_the_background);
  RUN_TEST(test_ramp_stretches_between_the_ink);
  RUN_TEST(test_ramp_varies_within_one_glyph);
  RUN_TEST(test_ramp_span_repeats);
  RUN_TEST(test_ramp_origin_shifts_by_whole_spans);
  RUN_TEST(test_ramp_origin_handles_negatives);
  RUN_TEST(test_glyph_colours_paint_per_glyph);
  RUN_TEST(test_empty_ramp_falls_back_to_flat);
  return UNITY_END();
}
