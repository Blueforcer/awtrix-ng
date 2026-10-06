#include <unity.h>
#include "../Visuals.h"

#include <cstdint>

#include "core/apps/ClockText.h"
#include "core/apps/builtin/BatteryApp.h"
#include "core/apps/builtin/DateApp.h"
#include "core/apps/builtin/HumidityApp.h"
#include "core/apps/builtin/TempApp.h"
#include "core/apps/builtin/TimeApp.h"
#include "core/render/BootScreen.h"
#include "core/render/ProvisioningScreen.h"
#include "core/render/TextRenderer.h"
#include "media/AwtrixFontAdapter.h"

using namespace awtrix;

#define G {0, 3, 3, 4, 0, 0}
static const FontGlyph kG[] = {G, G, G, G, G, G, G, G, G, G,
                               G, G, G, G, G, G, G, G, G, G};
#undef G
static const uint8_t kB[] = {0xFF, 0x80};
static const GfxFont kFont = {kB, kG, '.', 'A', 8};

void setUp() {}
void tearDown() {}

static RenderCtx ctxFor(const Settings& s, const RuntimeState& rt) {
  RenderCtx ctx;
  ctx.settings = &s;
  ctx.runtime = &rt;
  ctx.font = &kFont;
  return ctx;
}

static uint32_t inkSum(const Canvas& c) {
  uint32_t sum = 0;
  for (int y = 0; y < c.height(); ++y)
    for (int x = 0; x < c.width(); ++x) {
      const uint32_t p = c.getPixel(x, y);
      sum += ((p >> 16) & 0xFFu) + ((p >> 8) & 0xFFu) + (p & 0xFFu);
    }
  return sum;
}

static uint32_t timeInk(Settings& s, int second, int64_t nowMs) {
  RuntimeState rt;
  RenderCtx ctx = ctxFor(s, rt);
  ctx.hour = 11;
  ctx.minute = 11;
  ctx.second = second;
  ctx.nowMs = nowMs;
  Canvas c(32, 8);
  TimeApp app;
  app.render(c, ctx);
  return inkSum(c);
}

static void test_timeapp_separator_blinks() {
  Settings s;
  s.textColor = 0xFF0000u;
  s.timeColor = OptColor{};
  s.weekdayBar.show = false;
  for (int mode : {0, 5}) {
    s.timeMode = mode;
    s.timeSeparatorMode = kSepBlink;
    const uint32_t lit = timeInk(s, 0, 0);
    const uint32_t dark = timeInk(s, 1, 0);
    TEST_ASSERT_TRUE(dark > 0u && dark < lit);
    s.timeSeparatorMode = kSepSteady;
    TEST_ASSERT_EQUAL_UINT32(timeInk(s, 0, 0), timeInk(s, 1, 0));
  }
}

static void test_timeapp_separator_pulses() {
  Settings s;
  s.textColor = 0xFF0000u;
  s.timeColor = OptColor{};
  s.timeMode = 0;
  s.weekdayBar.show = false;
  s.timeSeparatorMode = kSepPulse;
  const uint32_t dimmed = timeInk(s, 0, 500);
  const uint32_t full = timeInk(s, 0, 1000);
  TEST_ASSERT_TRUE(dimmed > 0u && dimmed < full);
}

static void test_timeapp_12h_no_leading_zero() {
  Settings s;
  s.textColor = 0xFF0000u;
  s.timeColor = OptColor{};
  s.timeMode = 0;
  s.weekdayBar.show = false;
  s.time24h = false;
  s.timeLeadingZero = false;
  s.timeSeparatorMode = kSepSteady;
  RuntimeState rt;
  RenderCtx ctx = ctxFor(s, rt);
  ctx.hour = 13;
  ctx.minute = 5;
  Canvas c(32, 8);
  TimeApp app;
  app.render(c, ctx);
  ctx.font = &awtrixFont();
  c.clear();
  app.render(c, ctx);
  TEST_ASSERT_TRUE(test::findText(c, awtrixFont(), "1:05").found());
  TEST_ASSERT_FALSE(test::findText(c, awtrixFont(), "01:05").found());
}

static constexpr uint32_t kTimeInk = 0x12ABEFu;

static int timeTextTop(Settings& s, bool weekdayBar) {
  s.weekdayBar.show = weekdayBar;
  RuntimeState rt;
  RenderCtx ctx = ctxFor(s, rt);
  ctx.font = &awtrixFont();
  ctx.hour = 11;
  ctx.minute = 11;
  ctx.mday = 15;
  Canvas c(32, 8);
  TimeApp().render(c, ctx);
  const auto time = test::findText(c, awtrixFont(), "11:11", [](uint32_t p) { return p == kTimeInk; });
  TEST_ASSERT_TRUE(time.found());
  return time.top;
}

static void test_timeapp_text_moves_down_only_under_a_top_weekday_bar() {
  Settings s;
  s.textColor = kTimeInk;
  s.timeColor = OptColor{};
  s.timeSeparatorMode = kSepSteady;
  s.timeMode = 0;
  const int plain = timeTextTop(s, false);
  for (int mode : {0, 1, 2, 3, 4}) {
    s.timeMode = mode;
    const bool barOnTop = mode == 2 || mode == 4;
    TEST_ASSERT_EQUAL_INT(plain, timeTextTop(s, false));
    TEST_ASSERT_EQUAL_INT(plain + (barOnTop ? 1 : 0), timeTextTop(s, true));
  }
}

static void test_dateapp_uses_date_color() {
  Settings s;
  s.dateColor = OptColor{0x00FF00u, true};
  s.dateWeekdayBar.show = false;
  RuntimeState rt;
  RenderCtx ctx = ctxFor(s, rt);
  Canvas c(32, 8);
  DateApp app;
  app.render(c, ctx);
  TEST_ASSERT_EQUAL_STRING("Date", app.id().c_str());
  TEST_ASSERT_TRUE(test::countPixels(c, test::green) > 0);
  TEST_ASSERT_EQUAL_INT(test::countPixels(c, test::lit), test::countPixels(c, test::green));
}

static void test_clock_and_date_draw_their_own_weekday_bars() {
  Settings s;
  s.textColor = 0x000000u;
  s.timeMode = 0;
  s.weekdayBar.show = true;
  s.weekdayBar.activeColor = s.weekdayBar.inactiveColor =
      s.weekdayBar.weekendActiveColor = s.weekdayBar.weekendInactiveColor = 0xFF0000u;
  s.dateWeekdayBar.show = true;
  s.dateWeekdayBar.activeColor = s.dateWeekdayBar.inactiveColor =
      s.dateWeekdayBar.weekendActiveColor = s.dateWeekdayBar.weekendInactiveColor = 0x00FF00u;
  RuntimeState rt;
  const RenderCtx ctx = ctxFor(s, rt);
  TimeApp time;
  DateApp date;
  for (bool showClock : {false, true}) {
    s.weekdayBar.show = showClock;
    for (bool showDate : {false, true}) {
      s.dateWeekdayBar.show = showDate;
      Canvas clockCanvas(32, 8), dateCanvas(32, 8);
      time.render(clockCanvas, ctx);
      date.render(dateCanvas, ctx);
      TEST_ASSERT_EQUAL(showClock, test::countPixels(clockCanvas, test::red) > 0);
      TEST_ASSERT_EQUAL(showDate, test::countPixels(dateCanvas, test::green) > 0);
      TEST_ASSERT_EQUAL_INT(0, test::countPixels(clockCanvas, test::green));
      TEST_ASSERT_EQUAL_INT(0, test::countPixels(dateCanvas, test::red));
    }
  }
}

static void inkRange(const Canvas& c, int from, int to, int& l, int& r) {
  l = -1;
  r = -1;
  for (int x = from; x < to; ++x)
    for (int y = 0; y < 8; ++y)
      if (c.getPixel(x, y) != 0u) {
        if (l < 0) l = x;
        r = x;
        break;
      }
}

static void assertShiftedCopy(const Canvas& narrow, const Canvas& wide) {
  const int shift = (wide.width() - narrow.width()) / 2;
  for (int y = 0; y < 8; ++y)
    for (int x = 0; x < wide.width(); ++x) {
      const int sx = x - shift;
      const uint32_t want =
          (sx >= 0 && sx < narrow.width()) ? narrow.getPixel(sx, y) : 0u;
      TEST_ASSERT_EQUAL_HEX32(want, wide.getPixel(x, y));
    }
}

static void test_icon_apps_translate_to_the_centre() {
  Settings s;
  RuntimeState rt;
  rt.batteryPercent = 77;
  RenderCtx ctx = ctxFor(s, rt);
  TempApp temp;
  HumidityApp hum;
  BatteryApp bat;
  IApp* apps[3] = {&temp, &hum, &bat};
  for (IApp* app : apps)
    for (int w : {40, 64, 128}) {
      Canvas narrow(32, 8);
      Canvas wide(w, 8);
      app->render(narrow, ctx);
      app->render(wide, ctx);
      assertShiftedCopy(narrow, wide);
    }
}

static void test_calendar_box_translates_to_the_centre() {
  Settings s;
  s.timeMode = 1;
  s.weekdayBar.show = true;
  s.weekdayBar.activeColor = 0xFF0000u;
  s.weekdayBar.inactiveColor = 0x111111u;
  RuntimeState rt;
  RenderCtx ctx = ctxFor(s, rt);
  ctx.mday = 15;
  TimeApp app;
  for (int w : {40, 64, 128}) {
    Canvas narrow(32, 8);
    Canvas wide(w, 8);
    app.render(narrow, ctx);
    app.render(wide, ctx);
    assertShiftedCopy(narrow, wide);
  }
}

static void test_icon_block_grows_with_long_text() {
  Settings s;
  RuntimeState rt;
  RenderCtx ctx = ctxFor(s, rt);
  HumidityApp app;

  Canvas shortText(64, 8);
  rt.humidity = 42.0f;
  app.render(shortText, ctx);
  int shortL, shortR;
  inkRange(shortText, 0, 64, shortL, shortR);

  Canvas longText(64, 8);
  rt.humidity = 1234567.0f;
  app.render(longText, ctx);
  int longL, longR;
  inkRange(longText, 0, 64, longL, longR);

  TEST_ASSERT_TRUE(longL > 0 && longL < shortL);
  TEST_ASSERT_TRUE(longR > shortR && longR < 63);
}

static void test_sensor_apps_draw_distinct_visible_icons() {
  Settings settings;
  settings.textColor = 0;
  RuntimeState state;
  const RenderCtx ctx = ctxFor(settings, state);
  Canvas temperature(32, 8), humidity(32, 8);
  TempApp().render(temperature, ctx);
  HumidityApp().render(humidity, ctx);
  TEST_ASSERT_TRUE(test::countPixels(temperature, test::lit) > 0);
  TEST_ASSERT_TRUE(test::countPixels(humidity, test::lit) > 0);
  TEST_ASSERT_FALSE(test::sameFrame(temperature, humidity));
}

static void test_battery_gauge_fills_with_charge() {
  Settings s;
  s.textColor = 0;
  RuntimeState rt;
  RenderCtx ctx = ctxFor(s, rt);
  BatteryApp bat;
  TEST_ASSERT_EQUAL_STRING("Battery", bat.id().c_str());
  int previous = -1;
  for (int percent : {0, 50, 100}) {
    rt.batteryPercent = percent;
    Canvas c(32, 8);
    bat.render(c, ctx);
    const int lit = test::countPixels(c, test::lit);
    TEST_ASSERT_TRUE(lit > previous);
    previous = lit;
  }
}

static void test_provisioning_screen() {
  Canvas first(32, 8), repeated(32, 8), later(32, 8);
  render::drawProvisioningScreen(first, kFont, 0);
  render::drawProvisioningScreen(repeated, kFont, 0);
  render::drawProvisioningScreen(later, kFont, 2560);
  TEST_ASSERT_TRUE(test::countPixels(first, test::lit) > 0);
  TEST_ASSERT_TRUE(test::sameFrame(first, repeated));
  TEST_ASSERT_FALSE(test::sameFrame(first, later));
}

static const GfxFont& bootFont() {
  static FontGlyph glyphs[95];
  static const bool init = [] {
    for (FontGlyph& g : glyphs) g = FontGlyph{0, 3, 3, 4, 0, 0};
    glyphs[0] = FontGlyph{0, 0, 0, 4, 0, 0};
    return true;
  }();
  (void)init;
  static const GfxFont f = {kB, glyphs, ' ', '~', 8};
  return f;
}

static int litCount(const Canvas& c) {
  int n = 0;
  for (int x = 0; x < c.width(); ++x)
    for (int y = 0; y < c.height(); ++y) n += c.getPixel(x, y) != 0u ? 1 : 0;
  return n;
}

// Bright foreground is distinguished from the dim, animated star field.
static const int kStarCeiling = 128;

static int brightest(uint32_t c) {
  const int r = (c >> 16) & 0xFF, g = (c >> 8) & 0xFF, b = c & 0xFF;
  return r > g ? (r > b ? r : b) : (g > b ? g : b);
}

// Anything above the star ceiling is logo, not background. The settled logo is
// hand drawn inside BootScreen, so the tests read it off the canvas rather than
// mirroring its shape.
static void brightMask(const Canvas& c, bool* out) {
  for (int y = 0; y < 8; ++y)
    for (int x = 0; x < 32; ++x) out[y * 32 + x] = brightest(c.getPixel(x, y)) > kStarCeiling;
}

static void test_boot_logo_animates_and_settles_within_the_display() {
  constexpr std::size_t pixels = 32 * 8;
  uint32_t guarded[pixels + 2] = {};
  guarded[0] = guarded[pixels + 1] = 0xDEADBEEFu;
  Canvas c(32, 8, guarded + 1), moving(32, 8), settled(32, 8);
  render::drawBootLogo(c, bootFont(), 0, 0);
  render::drawBootLogo(moving, bootFont(), 0, render::kBootIntroMs / 3);
  TEST_ASSERT_TRUE(litCount(moving) > litCount(c));
  render::drawBootLogo(c, bootFont(), 0, render::kBootIntroMs);
  render::drawBootLogo(settled, bootFont(), 0, render::kBootIntroMs + 500);
  bool mask[pixels], later[pixels];
  brightMask(c, mask);
  brightMask(settled, later);
  TEST_ASSERT_EQUAL_MEMORY(mask, later, sizeof(mask));
  TEST_ASSERT_TRUE(test::bounds(c, [](uint32_t p) { return brightest(p) > kStarCeiling; }).found());
  TEST_ASSERT_EQUAL_HEX32(0xDEADBEEFu, guarded[0]);
  TEST_ASSERT_EQUAL_HEX32(0xDEADBEEFu, guarded[pixels + 1]);
}

static void test_boot_logo_centres_on_taller_panels() {
  constexpr int64_t kStanding = 1700;
  Canvas c(32, 16);
  render::drawBootLogo(c, awtrixFont(), 0, kStanding);
  int top = -1, bottom = -1;
  for (int y = 0; y < c.height(); ++y)
    for (int x = 0; x < c.width(); ++x)
      if (brightest(c.getPixel(x, y)) > kStarCeiling) {
        if (top < 0) top = y;
        bottom = y;
      }
  TEST_ASSERT_TRUE(top >= 0);
  const int above = top, below = c.height() - 1 - bottom;
  TEST_ASSERT_TRUE(above - below <= 1 && below - above <= 1);
}

static void test_boot_info_line_timing() {
  const int panel = 32, shortLine = 12, longLine = 40;
  const int centered = render::bootInfoLineX(panel, shortLine, 0);
  TEST_ASSERT_INT_WITHIN(1, centered, panel - centered - shortLine);
  TEST_ASSERT_EQUAL(centered, render::bootInfoLineX(panel, shortLine, 99999));
  TEST_ASSERT_TRUE(render::bootInfoLineShowing(panel, shortLine, render::kBootInfoHoldMs - 1));
  TEST_ASSERT_FALSE(render::bootInfoLineShowing(panel, shortLine, render::kBootInfoHoldMs));
  TEST_ASSERT_EQUAL(panel, render::bootInfoLineX(panel, longLine, -500));
  TEST_ASSERT_EQUAL(panel, render::bootInfoLineX(panel, longLine, 0));
  TEST_ASSERT_TRUE(render::bootInfoLineX(panel, longLine, 1000) < panel);
  TEST_ASSERT_EQUAL(-longLine, render::bootInfoLineX(panel, longLine, INT64_MAX / 64));
  TEST_ASSERT_FALSE(render::bootInfoLineShowing(panel, longLine, INT64_MAX / 64));
}

static void test_boot_info_scrolls_a_long_address_through_once() {
  constexpr std::size_t pixels = 32 * 8;
  uint32_t guarded[pixels + 2] = {};
  guarded[0] = guarded[pixels + 1] = 0xDEADBEEFu;
  Canvas c(32, 8, guarded + 1), early(32, 8);
  const render::BootInfo info{"1.1.2", "192.168.1.42"};
  TEST_ASSERT_TRUE(render::drawBootInfo(c, awtrixFont(), info, 1000, 1000));
  TEST_ASSERT_EQUAL(0, litCount(c));
  TEST_ASSERT_TRUE(render::drawBootInfo(early, awtrixFont(), info, 1000, 1700));
  TEST_ASSERT_TRUE(litCount(early) > 0);
  TEST_ASSERT_TRUE(render::drawBootInfo(c, awtrixFont(), info, 1000, 2000));
  TEST_ASSERT_FALSE(test::sameShape(early, c));
  int64_t t = 0;
  while (t < 30000 && render::drawBootInfo(c, awtrixFont(), info, 0, t)) t += 10;
  TEST_ASSERT_TRUE(t > 0 && t < 30000);
  TEST_ASSERT_EQUAL(0, litCount(c));
  TEST_ASSERT_EQUAL_HEX32(0xDEADBEEFu, guarded[0]);
  TEST_ASSERT_EQUAL_HEX32(0xDEADBEEFu, guarded[pixels + 1]);
}

static void test_boot_info_holds_what_fits() {
  Canvas c(64, 8), first(64, 8);
  const render::BootInfo info{"1.1.2", "192.168.1.42"};
  render::drawBootInfo(first, awtrixFont(), info, 0, 0);
  for (int64_t t : {int64_t{0}, int64_t{1500}, render::kBootInfoHoldMs - 1}) {
    TEST_ASSERT_TRUE(render::drawBootInfo(c, awtrixFont(), info, 0, t));
    const auto label = test::findText(c, awtrixFont(), info.address);
    TEST_ASSERT_TRUE(label.found());
    TEST_ASSERT_INT_WITHIN(1, label.left, c.width() - 1 - label.right);
    TEST_ASSERT_TRUE(test::sameShape(first, c));
  }
  TEST_ASSERT_FALSE(render::drawBootInfo(c, awtrixFont(), info, 0, render::kBootInfoHoldMs));
}

static void test_boot_info_puts_the_version_above_the_address() {
  Canvas c(64, 16);
  const render::BootInfo info{"1.1.2", "192.168.1.42"};
  render::drawBootInfo(c, awtrixFont(), info, 0, 0);
  const auto version = test::findText(c, awtrixFont(), info.version);
  const auto address = test::findText(c, awtrixFont(), info.address);
  TEST_ASSERT_TRUE(version.found() && address.found());
  TEST_ASSERT_TRUE(version.bottom < address.top);
}

static void test_boot_info_keeps_the_version_while_the_address_scrolls() {
  Canvas c(32, 16);
  const render::BootInfo info{"1.1.2", "192.168.178.123:8080"};
  const int64_t late = render::kBootInfoHoldMs + 200;
  TEST_ASSERT_TRUE(render::drawBootInfo(c, awtrixFont(), info, 0, late));
  TEST_ASSERT_TRUE(test::findText(c, awtrixFont(), info.version).found());
  int64_t t = late;
  while (t < 30000 && render::drawBootInfo(c, awtrixFont(), info, 0, t)) {
    TEST_ASSERT_TRUE(test::findText(c, awtrixFont(), info.version).found());
    t += 10;
  }
  TEST_ASSERT_TRUE(t < 30000);
}

static void test_boot_info_centres_its_rows_on_tall_panels() {
  Canvas c(64, 32);
  const render::BootInfo info{"1.1.2", "192.168.1.42"};
  TEST_ASSERT_TRUE(render::drawBootInfo(c, awtrixFont(), info, 0, 100));
  const auto box = test::bounds(c, test::lit);
  TEST_ASSERT_TRUE(box.found());
  TEST_ASSERT_INT_WITHIN(1, box.top, c.height() - 1 - box.bottom);
}

static void test_boot_info_without_an_address_shows_the_version() {
  const render::BootInfo info{"1.1.2", ""};
  for (int height : {8, 16}) {
    Canvas c(32, height);
    TEST_ASSERT_TRUE(render::drawBootInfo(c, awtrixFont(), info, 0, 0));
    const auto label = test::findText(c, awtrixFont(), info.version);
    TEST_ASSERT_TRUE(label.found());
    TEST_ASSERT_INT_WITHIN(1, label.top, c.height() - 1 - label.bottom);
  }
}

static void test_boot_info_with_nothing_to_show_ends_at_once() {
  Canvas c(32, 8);
  TEST_ASSERT_FALSE(render::drawBootInfo(c, awtrixFont(), render::BootInfo{}, 0, 0));
  TEST_ASSERT_EQUAL(0, litCount(c));
}

int main(int, char**) {
  UNITY_BEGIN();
  RUN_TEST(test_timeapp_separator_blinks);
  RUN_TEST(test_timeapp_separator_pulses);
  RUN_TEST(test_timeapp_12h_no_leading_zero);
  RUN_TEST(test_timeapp_text_moves_down_only_under_a_top_weekday_bar);
  RUN_TEST(test_dateapp_uses_date_color);
  RUN_TEST(test_clock_and_date_draw_their_own_weekday_bars);
  RUN_TEST(test_icon_apps_translate_to_the_centre);
  RUN_TEST(test_calendar_box_translates_to_the_centre);
  RUN_TEST(test_icon_block_grows_with_long_text);
  RUN_TEST(test_sensor_apps_draw_distinct_visible_icons);
  RUN_TEST(test_battery_gauge_fills_with_charge);
  RUN_TEST(test_provisioning_screen);
  RUN_TEST(test_boot_logo_animates_and_settles_within_the_display);
  RUN_TEST(test_boot_logo_centres_on_taller_panels);
  RUN_TEST(test_boot_info_line_timing);
  RUN_TEST(test_boot_info_scrolls_a_long_address_through_once);
  RUN_TEST(test_boot_info_holds_what_fits);
  RUN_TEST(test_boot_info_puts_the_version_above_the_address);
  RUN_TEST(test_boot_info_keeps_the_version_while_the_address_scrolls);
  RUN_TEST(test_boot_info_centres_its_rows_on_tall_panels);
  RUN_TEST(test_boot_info_without_an_address_shows_the_version);
  RUN_TEST(test_boot_info_with_nothing_to_show_ends_at_once);
  return UNITY_END();
}
