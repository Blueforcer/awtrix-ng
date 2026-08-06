#include <unity.h>

#include <string>

#include "core/Settings.h"
#include "core/apps/IApp.h"
#include "core/render/Canvas.h"
#include "core/script/BerryVM.h"
#include "core/script/ScriptBindings.h"
#include "core/script/ScriptServices.h"
#include "core/script/ScrollBank.h"

using namespace awtrix;

static const FontGlyph kG[] = {{0, 3, 3, 4, 0, 0}};
static const uint8_t kB[] = {0xFF, 0x80};
static const GfxFont kFont = {kB, kG, 'A', 'A', 8};

static const FontGlyph kLargeG[] = {{0, 5, 3, 6, 0, 0}};
static const uint8_t kLargeB[] = {0xFF, 0xFF};
static const GfxFont kLargeFont = {kLargeB, kLargeG, 'A', 'A', 8};

static script::ScriptServices g_svc;
static Settings g_settings;
static long g_ms = 0;

void setUp() {
  g_ms = 0;
  g_settings = Settings{};
  g_settings.textColor = 0x00FF00u;
  g_settings.scrollDefaults.holdMs = 0;
  g_svc.monotonicMs = [] { return g_ms; };
  g_svc.settings = [] { return &g_settings; };
  script::setServices(&g_svc);
}

void tearDown() { script::setServices(nullptr); }

struct Panel {
  script::BerryVM vm;
  script::ScrollBank bank;
  RenderCtx ctx;

  bool load(const char* body) {
    std::string err;
    if (!script::installBindings(vm, err)) {
      TEST_MESSAGE(err.c_str());
      return false;
    }
    ctx.font = &kFont;
    ctx.fonts[0] = &kFont;
    ctx.fonts[1] = &kLargeFont;
    if (!vm.load(body)) {
      TEST_MESSAGE(vm.lastError().c_str());
      return false;
    }
    return true;
  }

  void frame(Canvas& c, long atMs) {
    g_ms = atMs;
    bank.beginFrame();
    script::BindingScope scope(&c, &ctx, "T", &bank);
    TEST_ASSERT_TRUE(vm.call("draw"));
  }
};

static int litCount(const Canvas& c) {
  int n = 0;
  for (std::size_t i = 0; i < c.size(); ++i)
    if (c.data()[i]) ++n;
  return n;
}

static bool differ(const Canvas& a, const Canvas& b) {
  for (std::size_t i = 0; i < a.size(); ++i)
    if (a.data()[i] != b.data()[i]) return true;
  return false;
}

static void test_short_form_centres_text_that_fits() {
  Panel p;
  TEST_ASSERT_TRUE(p.load("def draw() scroll_text('AA', 0xFFFFFF) end"));
  Canvas a(32, 8);
  p.frame(a, 0);
  TEST_ASSERT_EQUAL_HEX32(0xFFFFFFu, a.getPixel(12, 6));

  Canvas b(32, 8);
  p.frame(b, 5000);
  TEST_ASSERT_FALSE_MESSAGE(differ(a, b), "text that fits must not move");
}

static void test_short_form_takes_the_device_text_colour() {
  Panel p;
  TEST_ASSERT_TRUE(p.load("def draw() scroll_text('AA') end"));
  Canvas c(32, 8);
  p.frame(c, 0);
  TEST_ASSERT_EQUAL_HEX32(0x00FF00u, c.getPixel(12, 6));
}

static void test_overflowing_text_moves_over_time() {
  Panel p;
  TEST_ASSERT_TRUE(p.load("def draw() scroll_text('AAAAAAAAAA', 0xFFFFFF) end"));
  Canvas a(32, 8);
  p.frame(a, 0);
  Canvas b(32, 8);
  p.frame(b, 100);

  TEST_ASSERT_TRUE(litCount(a) > 0);
  TEST_ASSERT_TRUE(differ(a, b));
}

static void test_the_box_form_keeps_bounce_off_the_icon() {
  Panel p;
  TEST_ASSERT_TRUE(p.load(
      "def draw() scroll_text(9, 6, width() - 9, 'AAAAAAAAAA', 0xFFFFFF, {'mode': 'bounce'}) end"));
  for (long ms = 0; ms <= 6000; ms += 100) {
    Canvas c(32, 8);
    p.frame(c, ms);
    for (int y = 0; y < 8; ++y)
      for (int x = 0; x < 9; ++x)
        TEST_ASSERT_EQUAL_HEX32_MESSAGE(0u, c.getPixel(x, y), "nothing may enter the icon columns");
  }
}

static void test_a_long_line_holds_the_turn_until_it_has_run() {
  Panel p;
  TEST_ASSERT_TRUE(p.load("def draw() scroll_text('AAAAAAAAAA', 0xFFFFFF, {'repeat': 1}) end"));
  Canvas c(32, 8);
  p.frame(c, 0);
  TEST_ASSERT_TRUE(p.bank.wantsMoreTime());

  for (long ms = 100; ms <= 20000; ms += 100) p.frame(c, ms);
  TEST_ASSERT_FALSE(p.bank.wantsMoreTime());
}

static void test_repeat_zero_never_holds_the_turn() {
  Panel p;
  TEST_ASSERT_TRUE(p.load("def draw() scroll_text('AAAAAAAAAA', 0xFFFFFF, {'repeat': 0}) end"));
  Canvas c(32, 8);
  p.frame(c, 0);
  TEST_ASSERT_FALSE(p.bank.wantsMoreTime());
}

static void test_a_line_not_drawn_this_frame_stops_holding() {
  Panel p;
  TEST_ASSERT_TRUE(
      p.load("def draw() if now_ms() < 50 scroll_text('AAAAAAAAAA', 0xFFFFFF, {'repeat': 1}) end "
             "end"));
  Canvas c(32, 8);
  p.frame(c, 0);
  TEST_ASSERT_TRUE(p.bank.wantsMoreTime());

  p.frame(c, 100);
  TEST_ASSERT_FALSE(p.bank.wantsMoreTime());
}

static void test_the_call_reports_completed_runs() {
  Panel p;
  TEST_ASSERT_TRUE(p.load(
      "def draw()\n"
      "  var runs = scroll_text('AAAAAAAAAA', 0xFFFFFF)\n"
      "  pixel(31, 0, runs >= 1 ? 0x0000FF : 0)\n"
      "end"));
  Canvas c(32, 8);
  p.frame(c, 0);
  TEST_ASSERT_EQUAL_HEX32_MESSAGE(0u, c.getPixel(31, 0), "nothing has run through yet");

  for (long ms = 100; ms <= 20000; ms += 100) p.frame(c, ms);
  TEST_ASSERT_EQUAL_HEX32_MESSAGE(0x0000FFu, c.getPixel(31, 0), "a completed pass must be reported");
}

static void test_arguments_matching_neither_shape_draw_nothing() {
  Panel p;
  TEST_ASSERT_TRUE(p.load("def draw() scroll_text(6, 'AAAAAAAAAA', 0xFFFFFF, 21) end"));
  Canvas c(32, 8);
  p.frame(c, 0);
  TEST_ASSERT_EQUAL_INT(0, litCount(c));
  TEST_ASSERT_FALSE(p.bank.wantsMoreTime());
}

static void test_two_lines_scroll_independently() {
  Panel p;
  TEST_ASSERT_TRUE(p.load(
      "def draw()\n"
      "  scroll_text(0, 1, width(), 'AAAAAAAAAA', 0xFFFFFF)\n"
      "  scroll_text(0, 7, width(), 'AAAAAAAAAA', 0xFF0000, {'mode': 'static'})\n"
      "end"));
  Canvas a(32, 8);
  p.frame(a, 0);
  Canvas b(32, 8);
  p.frame(b, 300);

  bool topMoved = false;
  for (int x = 0; x < 32; ++x) topMoved |= a.getPixel(x, 1) != b.getPixel(x, 1);
  TEST_ASSERT_TRUE_MESSAGE(topMoved, "the scrolling line must move");

  for (int x = 0; x < 32; ++x)
    TEST_ASSERT_EQUAL_HEX32_MESSAGE(a.getPixel(x, 7), b.getPixel(x, 7),
                                    "the static line must not move");
}

static void test_fragments_colour_each_run() {
  Panel p;
  TEST_ASSERT_TRUE(p.load("def draw() scroll_text([['A', 0xFF0000], ['A', 0x00FF00]]) end"));
  Canvas c(32, 8);
  p.frame(c, 0);
  TEST_ASSERT_EQUAL_HEX32(0xFF0000u, c.getPixel(12, 6));
  TEST_ASSERT_EQUAL_HEX32(0x00FF00u, c.getPixel(16, 6));
}

static void test_a_fragment_without_a_colour_takes_the_run_colour() {
  Panel p;
  TEST_ASSERT_TRUE(p.load("def draw() scroll_text([['A'], ['A', 0x00FF00]], 0xFF0000) end"));
  Canvas c(32, 8);
  p.frame(c, 0);
  TEST_ASSERT_EQUAL_HEX32(0xFF0000u, c.getPixel(12, 6));
  TEST_ASSERT_EQUAL_HEX32(0x00FF00u, c.getPixel(16, 6));
}

static void test_fragments_follow_the_large_font() {
  Panel p;
  TEST_ASSERT_TRUE(
      p.load("def draw() font('large') scroll_text([['A', 0xFF0000], ['A', 0x00FF00]]) end"));
  Canvas c(32, 8);
  p.frame(c, 0);
  TEST_ASSERT_EQUAL_HEX32_MESSAGE(0xFF0000u, c.getPixel(10, 6),
                                  "the wider glyph must set where the first fragment starts");
  TEST_ASSERT_EQUAL_HEX32(0x00FF00u, c.getPixel(16, 6));
}

static void test_the_box_form_takes_fragments_too() {
  Panel p;
  TEST_ASSERT_TRUE(p.load(
      "def draw() scroll_text(9, 6, width() - 9, [['A', 0xFF0000], ['A', 0x00FF00]], 0xFFFFFF) "
      "end"));
  Canvas c(32, 8);
  p.frame(c, 0);
  TEST_ASSERT_EQUAL_HEX32(0xFF0000u, c.getPixel(18, 6));
  TEST_ASSERT_EQUAL_HEX32(0x00FF00u, c.getPixel(22, 6));
}

static void test_two_fragment_lines_keep_their_own_colours() {
  Panel p;
  TEST_ASSERT_TRUE(p.load(
      "def draw()\n"
      "  scroll_text(0, 1, width(), [['A', 0xFF0000]], 0xFFFFFF, {'mode': 'static'})\n"
      "  scroll_text(0, 7, width(), [['A', 0x00FF00]], 0xFFFFFF, {'mode': 'static'})\n"
      "end"));
  Canvas c(32, 8);
  p.frame(c, 0);
  TEST_ASSERT_EQUAL_HEX32(0xFF0000u, c.getPixel(14, 1));
  TEST_ASSERT_EQUAL_HEX32(0x00FF00u, c.getPixel(14, 7));
}

static void test_a_fragment_list_scrolls_when_it_overflows() {
  Panel p;
  TEST_ASSERT_TRUE(p.load(
      "def draw() scroll_text([['AAAAA', 0xFF0000], ['AAAAA', 0x00FF00]], 0xFFFFFF) end"));
  Canvas a(32, 8);
  p.frame(a, 0);
  Canvas b(32, 8);
  p.frame(b, 100);
  TEST_ASSERT_TRUE(litCount(a) > 0);
  TEST_ASSERT_TRUE(differ(a, b));
}

int main(int, char**) {
  UNITY_BEGIN();
  RUN_TEST(test_short_form_centres_text_that_fits);
  RUN_TEST(test_short_form_takes_the_device_text_colour);
  RUN_TEST(test_overflowing_text_moves_over_time);
  RUN_TEST(test_the_box_form_keeps_bounce_off_the_icon);
  RUN_TEST(test_a_long_line_holds_the_turn_until_it_has_run);
  RUN_TEST(test_repeat_zero_never_holds_the_turn);
  RUN_TEST(test_a_line_not_drawn_this_frame_stops_holding);
  RUN_TEST(test_the_call_reports_completed_runs);
  RUN_TEST(test_arguments_matching_neither_shape_draw_nothing);
  RUN_TEST(test_two_lines_scroll_independently);
  RUN_TEST(test_fragments_colour_each_run);
  RUN_TEST(test_a_fragment_without_a_colour_takes_the_run_colour);
  RUN_TEST(test_fragments_follow_the_large_font);
  RUN_TEST(test_the_box_form_takes_fragments_too);
  RUN_TEST(test_two_fragment_lines_keep_their_own_colours);
  RUN_TEST(test_a_fragment_list_scrolls_when_it_overflows);
  return UNITY_END();
}
