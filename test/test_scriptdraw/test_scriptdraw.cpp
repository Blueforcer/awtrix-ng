#include <unity.h>

#include <string>

#include "core/apps/IApp.h"
#include "core/render/Canvas.h"
#include "core/script/BerryVM.h"
#include "core/script/ScriptBindings.h"
#include "core/script/ScriptServices.h"

using namespace awtrix;

static const FontGlyph kG[] = {{0, 3, 3, 4, 0, 0}};
static const uint8_t kB[] = {0xFF, 0x80};
static const GfxFont kFont = {kB, kG, 'A', 'A', 8};

static const FontGlyph* highGlyphs() {
  static FontGlyph g[0xC2 - 0xB0 + 1];
  for (FontGlyph& e : g) e = FontGlyph{0, 3, 3, 5, 0, 0};
  g[0] = FontGlyph{0, 3, 3, 3, 0, 0};
  return g;
}
static const GfxFont kHighFont = {kB, highGlyphs(), 0xB0, 0xC2, 8};

static const FontGlyph kWideG[] = {{0, 3, 3, 8, 0, 0}};
static const GfxFont kWideFont = {kB, kWideG, 'A', 'A', 8};

struct FakeIcon : script::IScriptIcon {
  long lastNowMs = -1;
  bool draw(Canvas& canvas, const std::string& name, int x, int y, int64_t nowMs) override {
    lastNowMs = nowMs;
    if (name == "missing") return false;
    canvas.setPixel(x, y, 0x00ABCDu);
    return true;
  }
};

static script::ScriptServices g_svc;
static FakeIcon g_icon;
static long g_ms = 0;

void setUp() {
  g_ms = 0;
  g_icon.lastNowMs = -1;
  g_svc.http = nullptr;
  g_svc.mqtt = nullptr;
  g_svc.icon = &g_icon;
  g_svc.storeSink = nullptr;
  g_svc.monotonicMs = [] { return g_ms; };
  script::setServices(&g_svc);
}

void tearDown() { script::setServices(nullptr); }

static bool load(script::BerryVM& vm, const char* user) {
  std::string err;
  if (!script::installBindings(vm, err)) {
    TEST_MESSAGE(err.c_str());
    return false;
  }
  if (!vm.load(user)) {
    TEST_MESSAGE(vm.lastError().c_str());
    return false;
  }
  return true;
}

static void drawWith(const GfxFont& font, const char* user, Canvas& c) {
  script::BerryVM vm;
  TEST_ASSERT_TRUE(load(vm, user));
  RenderCtx ctx;
  ctx.font = &font;
  ctx.fonts[0] = &font;
  ctx.fonts[1] = &kWideFont;
  script::BindingScope s(&c, &ctx, "T");
  TEST_ASSERT_TRUE(vm.call("draw"));
}

static void draw(const char* user, Canvas& c) { drawWith(kFont, user, c); }


static void test_fill_functions_use_the_shape_first_names() {
  Canvas c(32, 8);
  draw("def draw() rect_fill(0,0,4,4,0xFF0000) circle_fill(16,4,2,0x00FF00) end", c);
  TEST_ASSERT_EQUAL_HEX32(0xFF0000u, c.getPixel(1, 1));
  TEST_ASSERT_EQUAL_HEX32(0x00FF00u, c.getPixel(16, 4));
}

static void test_rgb_packs_channels() {
  Canvas c(32, 8);
  draw("def draw() pixel(0, 0, rgb(255, 128, 0)) end", c);
  TEST_ASSERT_EQUAL_HEX32(0xFF8000u, c.getPixel(0, 0));
}

static void test_hsv_matches_color_util() {
  Canvas c(32, 8);
  draw("def draw() pixel(0, 0, hsv(0, 100, 100)) end", c);
  draw("def draw() pixel(1, 0, hsv(120, 100, 100)) end", c);
  TEST_ASSERT_EQUAL_HEX32(0xFF0000u, c.getPixel(0, 0));
  TEST_ASSERT_EQUAL_HEX32(0x00FF00u, c.getPixel(1, 0));
}


static void test_ramp_text_from_a_stop_list() {
  Canvas c(32, 8);
  draw("def draw() ramp_text(0, 6, 'AA', [0xFF0000, 0x0000FF]) end", c);
  TEST_ASSERT_EQUAL_HEX32(0xFF0000u, c.getPixel(0, 6));
  TEST_ASSERT_EQUAL_HEX32(0x0000FFu, c.getPixel(6, 6));
}

static void test_ramp_text_from_a_stock_name() {
  Canvas c(32, 8);
  draw("def draw() ramp_text(0, 6, 'A', 'Rainbow') end", c);
  TEST_ASSERT_EQUAL_HEX32(0xFF0000u, c.getPixel(0, 6));
}

static void test_ramp_text_unknown_palette_draws_nothing() {
  Canvas c(32, 8);
  draw("def draw() ramp_text(0, 6, 'A', 'nosuchpalette') end", c);
  bool lit = false;
  for (std::size_t i = 0; i < c.size(); ++i) lit |= c.data()[i] != 0;
  TEST_ASSERT_FALSE(lit);
}

static void test_bar_chart_takes_a_palette_name() {
  Canvas c(32, 8);
  draw("def draw() bar_chart([4, 8], 'Heat') end", c);
  TEST_ASSERT_TRUE(c.getPixel(16, 0) != 0);
  TEST_ASSERT_TRUE(c.getPixel(0, 7) != c.getPixel(16, 0));
}


static void test_progress_defaults_match_pushed_app() {
  Canvas c(32, 8);
  draw("def draw() progress(50) end", c);
  TEST_ASSERT_EQUAL_HEX32(0x00FF00u, c.getPixel(0, 7));
  TEST_ASSERT_EQUAL_HEX32(0xFFFFFFu, c.getPixel(16, 7));
}

static void test_progress_custom_colours() {
  Canvas c(32, 8);
  draw("def draw() progress(50, 0x0000FF, 0x202020) end", c);
  TEST_ASSERT_EQUAL_HEX32(0x0000FFu, c.getPixel(0, 7));
  TEST_ASSERT_EQUAL_HEX32(0x202020u, c.getPixel(16, 7));
}

static void test_bar_chart_from_list() {
  Canvas c(32, 8);
  draw("def draw() bar_chart([4, 8], 0x00FF00) end", c);
  TEST_ASSERT_EQUAL_HEX32(0x00FF00u, c.getPixel(0, 7));
  TEST_ASSERT_EQUAL_HEX32(0u, c.getPixel(0, 3));
  TEST_ASSERT_EQUAL_HEX32(0x00FF00u, c.getPixel(16, 0));
}

static void test_line_chart_from_list() {
  Canvas c(32, 8);
  draw("def draw() line_chart([0, 8], 0x00FF00) end", c);
  TEST_ASSERT_EQUAL_HEX32(0x00FF00u, c.getPixel(0, 7));
  TEST_ASSERT_EQUAL_HEX32(0x00FF00u, c.getPixel(31, 0));
}

static void test_chart_default_colour_is_white() {
  Canvas c(32, 8);
  draw("def draw() bar_chart([8]) end", c);
  TEST_ASSERT_EQUAL_HEX32(0xFFFFFFu, c.getPixel(0, 0));
}

static void test_chart_reals_truncate_and_bad_entries_zero() {
  Canvas c(32, 8);
  draw("def draw() bar_chart([8.9, 'x'], 0x00FF00) end", c);
  TEST_ASSERT_EQUAL_HEX32(0x00FF00u, c.getPixel(0, 0));
}


static int litCount(Canvas& c) {
  int n = 0;
  for (std::size_t i = 0; i < c.size(); ++i)
    if (c.data()[i]) ++n;
  return n;
}

static void test_missing_colour_args_read_as_zero() {
  Canvas c(32, 8);
  draw("def draw() pixel(0, 0, rgb() == 0 ? 0x22 : 0x99) pixel(1, 0, hsv() == 0 ? 0x22 : 0x99) end",
       c);
  TEST_ASSERT_EQUAL_HEX32(0x22u, c.getPixel(0, 0));
  TEST_ASSERT_EQUAL_HEX32(0x22u, c.getPixel(1, 0));
}

static void test_progress_without_args_draws_nothing() {
  Canvas c(32, 8);
  draw("def draw() progress() end", c);
  TEST_ASSERT_EQUAL_INT(0, litCount(c));
}

static void test_icon_forwards_animation_clock() {
  Canvas c(32, 8);
  g_ms = 4200;
  draw("def draw() icon('clock', 3, 0) end", c);
  TEST_ASSERT_EQUAL_INT(4200, g_icon.lastNowMs);
  TEST_ASSERT_EQUAL_HEX32(0x00ABCDu, c.getPixel(3, 0));
}

static void test_icon_unknown_returns_false() {
  Canvas c(32, 8);
  draw("def draw() pixel(0, 0, icon('missing', 0, 0) ? 0xFF0000 : 0x00FF00) end", c);
  TEST_ASSERT_EQUAL_HEX32(0x00FF00u, c.getPixel(0, 0));
}

static void test_no_canvas_is_a_silent_noop() {
  script::BerryVM vm;
  TEST_ASSERT_TRUE(load(vm,
                        "def loop()\n"
                        "  bar_chart([1, 2, 3])\n"
                        "  line_chart([1, 2, 3])\n"
                        "  progress(10)\n"
                        "  ramp_text(0, 0, 'A', 'Heat')\n"
                        "end"));
  script::BindingScope s(nullptr, nullptr, "T");
  TEST_ASSERT_TRUE(vm.call("loop"));
}

static void test_utf8_text_folds_to_the_font_charset() {
  Canvas c(32, 8);
  drawWith(kHighFont, "def draw() pixel(0, 7, text_width('\xC2\xB0')) end", c);
  TEST_ASSERT_EQUAL_HEX32(3u, c.getPixel(0, 7));

  Canvas d(32, 8);
  drawWith(kHighFont, "def draw() pixel(0, 7, text(0, 0, '\xC2\xB0', 0xFFFFFF)) end", d);
  TEST_ASSERT_EQUAL_HEX32(3u, d.getPixel(0, 7));
}

static void test_utf8_ramp_text_folds_too() {
  Canvas c(32, 8);
  drawWith(kHighFont,
           "def draw() pixel(0, 7, ramp_text(0, 0, '\xC2\xB0', 'Heat')) end", c);
  TEST_ASSERT_EQUAL_HEX32(3u, c.getPixel(0, 7));
}

static void test_font_switches_drawing_and_measuring_together() {
  Canvas c(32, 8);
  drawWith(kFont, "def draw() pixel(0, 7, text_width('A')) end", c);
  TEST_ASSERT_EQUAL_HEX32(4u, c.getPixel(0, 7));

  Canvas d(32, 8);
  drawWith(kFont, "def draw() font('large') pixel(0, 7, text_width('A')) end", d);
  TEST_ASSERT_EQUAL_HEX32(8u, d.getPixel(0, 7));

  Canvas e(32, 8);
  drawWith(kFont, "def draw() font('large') pixel(0, 7, text(0, 0, 'A', 0xFFFFFF)) end", e);
  TEST_ASSERT_EQUAL_HEX32(8u, e.getPixel(0, 7));
}

static void test_an_unknown_font_name_is_ignored() {
  Canvas c(32, 8);
  drawWith(kFont, "def draw() font('huge') pixel(0, 7, text_width('A')) end", c);
  TEST_ASSERT_EQUAL_HEX32(4u, c.getPixel(0, 7));
}

int main(int, char**) {
  UNITY_BEGIN();
  RUN_TEST(test_font_switches_drawing_and_measuring_together);
  RUN_TEST(test_an_unknown_font_name_is_ignored);
  RUN_TEST(test_utf8_text_folds_to_the_font_charset);
  RUN_TEST(test_utf8_ramp_text_folds_too);
  RUN_TEST(test_fill_functions_use_the_shape_first_names);
  RUN_TEST(test_rgb_packs_channels);
  RUN_TEST(test_hsv_matches_color_util);
  RUN_TEST(test_ramp_text_from_a_stop_list);
  RUN_TEST(test_ramp_text_from_a_stock_name);
  RUN_TEST(test_ramp_text_unknown_palette_draws_nothing);
  RUN_TEST(test_bar_chart_takes_a_palette_name);
  RUN_TEST(test_progress_defaults_match_pushed_app);
  RUN_TEST(test_progress_custom_colours);
  RUN_TEST(test_bar_chart_from_list);
  RUN_TEST(test_line_chart_from_list);
  RUN_TEST(test_chart_default_colour_is_white);
  RUN_TEST(test_chart_reals_truncate_and_bad_entries_zero);
  RUN_TEST(test_missing_colour_args_read_as_zero);
  RUN_TEST(test_progress_without_args_draws_nothing);
  RUN_TEST(test_icon_forwards_animation_clock);
  RUN_TEST(test_icon_unknown_returns_false);
  RUN_TEST(test_no_canvas_is_a_silent_noop);
  return UNITY_END();
}
