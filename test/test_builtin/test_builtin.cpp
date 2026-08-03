#include <unity.h>

#include "core/apps/ClockText.h"
#include "core/apps/builtin/BatteryApp.h"
#include "core/apps/builtin/DateApp.h"
#include "core/apps/builtin/HumidityApp.h"
#include "core/apps/builtin/TempApp.h"
#include "core/apps/builtin/TimeApp.h"
#include "core/render/BootScreen.h"
#include "core/render/ProvisioningScreen.h"
#include "core/render/TextRenderer.h"

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

static void test_timeapp_renders_centered() {
  Settings s;
  s.timeSeparatorMode = kSepSteady;
  s.textColor = 0xFF0000u;
  s.timeColor = OptColor{};
  s.timeMode = 0;
  s.weekdayBar.show = false;
  RuntimeState rt;
  RenderCtx ctx = ctxFor(s, rt);
  Canvas c(32, 8);
  TimeApp app;
  app.render(c, ctx);
  TEST_ASSERT_EQUAL_STRING("Time", app.id().c_str());
  TEST_ASSERT_EQUAL_HEX32(0xFF0000u, c.getPixel(6, 6));
  TEST_ASSERT_EQUAL_HEX32(0xFF0000u, c.getPixel(14, 6));
}

static void test_timeapp_separator_blinks() {
  Settings s;
  s.textColor = 0xFF0000u;
  s.timeColor = OptColor{};
  s.timeMode = 0;
  s.weekdayBar.show = false;
  s.timeSeparatorMode = kSepBlink;
  RuntimeState rt;
  RenderCtx ctx = ctxFor(s, rt);
  ctx.second = 1;
  Canvas c(32, 8);
  TimeApp app;
  app.render(c, ctx);
  TEST_ASSERT_EQUAL_HEX32(0xFF0000u, c.getPixel(6, 6));
  TEST_ASSERT_EQUAL_HEX32(0x000000u, c.getPixel(14, 6));
  TEST_ASSERT_EQUAL_HEX32(0xFF0000u, c.getPixel(18, 6));
}

static void test_timeapp_separator_pulses() {
  Settings s;
  s.textColor = 0xFF0000u;
  s.timeColor = OptColor{};
  s.timeMode = 0;
  s.weekdayBar.show = false;
  s.timeSeparatorMode = kSepPulse;
  RuntimeState rt;
  RenderCtx ctx = ctxFor(s, rt);
  ctx.nowMs = 500;
  Canvas c(32, 8);
  TimeApp app;
  app.render(c, ctx);
  const uint32_t red = (c.getPixel(14, 6) >> 16) & 0xFFu;
  TEST_ASSERT_TRUE(red > 100 && red < 160);
  TEST_ASSERT_EQUAL_HEX32(0xFF0000u, c.getPixel(6, 6));
  ctx.nowMs = 1000;
  app.render(c, ctx);
  TEST_ASSERT_EQUAL_HEX32(0xFF0000u, c.getPixel(14, 6));
}

static void test_timeapp_12h_no_leading_zero() {
  Settings s;
  s.textColor = 0xFF0000u;
  s.timeColor = OptColor{};
  s.timeMode = 0;
  s.weekdayBar.show = false;
  s.time24h = false;
  s.timeLeadingZero = false;
  RuntimeState rt;
  RenderCtx ctx = ctxFor(s, rt);
  ctx.hour = 13;
  ctx.minute = 5;
  Canvas c(32, 8);
  TimeApp app;
  app.render(c, ctx);
  TEST_ASSERT_EQUAL_HEX32(0xFF0000u, c.getPixel(8, 6));
  TEST_ASSERT_EQUAL_HEX32(0x000000u, c.getPixel(6, 6));
}

static void test_dateapp_uses_date_color() {
  Settings s;
  s.dateColor = OptColor{0x00FF00u, true};
  s.weekdayBar.show = false;
  RuntimeState rt;
  RenderCtx ctx = ctxFor(s, rt);
  Canvas c(32, 8);
  DateApp app;
  app.render(c, ctx);
  TEST_ASSERT_EQUAL_STRING("Date", app.id().c_str());
  TEST_ASSERT_EQUAL_HEX32(0x00FF00u, c.getPixel(0, 6));
}

static void test_weekday_bar_centers_on_wide_matrix() {
  Settings s;
  s.timeMode = 0;
  s.weekdayBar.show = true;
  s.weekdayBar.startOnMonday = false;
  s.weekdayBar.weekendMask = 0;
  s.weekdayBar.activeColor = 0xFF0000u;
  s.weekdayBar.inactiveColor = 0x111111u;
  RuntimeState rt;
  RenderCtx ctx = ctxFor(s, rt);
  ctx.weekday = 0;
  Canvas c(64, 8);
  TimeApp app;
  app.render(c, ctx);
  TEST_ASSERT_EQUAL_HEX32(0u, c.getPixel(17, 7));
  TEST_ASSERT_EQUAL_HEX32(0xFF0000u, c.getPixel(18, 7));
  TEST_ASSERT_EQUAL_HEX32(0x111111u, c.getPixel(22, 7));
  TEST_ASSERT_EQUAL_HEX32(0x111111u, c.getPixel(44, 7));
  TEST_ASSERT_EQUAL_HEX32(0u, c.getPixel(45, 7));
}

static void test_weekday_bar_centers_beside_calendar_box() {
  Settings s;
  s.timeMode = 1;
  s.timeColor = OptColor{0x000000u, true};
  s.calendarBodyColor = 0x222222u;
  s.weekdayBar.show = true;
  s.weekdayBar.startOnMonday = false;
  s.weekdayBar.weekendMask = 0;
  s.weekdayBar.activeColor = 0xFF0000u;
  s.weekdayBar.inactiveColor = 0x111111u;
  RuntimeState rt;
  RenderCtx ctx = ctxFor(s, rt);
  ctx.weekday = 0;
  Canvas c(64, 8);
  TimeApp app;
  app.render(c, ctx);
  TEST_ASSERT_EQUAL_HEX32(0x222222u, c.getPixel(24, 7));
  TEST_ASSERT_EQUAL_HEX32(0u, c.getPixel(25, 7));
  TEST_ASSERT_EQUAL_HEX32(0xFF0000u, c.getPixel(26, 7));
  TEST_ASSERT_EQUAL_HEX32(0xFF0000u, c.getPixel(27, 7));
  TEST_ASSERT_EQUAL_HEX32(0u, c.getPixel(28, 7));
  TEST_ASSERT_EQUAL_HEX32(0x111111u, c.getPixel(29, 7));
}

static void test_dateapp_weekday_bar_centers_on_wide_matrix() {
  Settings s;
  s.dateColor = OptColor{0x000000u, true};
  s.weekdayBar.show = true;
  s.weekdayBar.startOnMonday = false;
  s.weekdayBar.weekendMask = 0;
  s.weekdayBar.activeColor = 0xFF0000u;
  s.weekdayBar.inactiveColor = 0x111111u;
  RuntimeState rt;
  RenderCtx ctx = ctxFor(s, rt);
  ctx.weekday = 0;
  Canvas c(64, 8);
  DateApp app;
  app.render(c, ctx);
  TEST_ASSERT_EQUAL_HEX32(0u, c.getPixel(17, 7));
  TEST_ASSERT_EQUAL_HEX32(0xFF0000u, c.getPixel(18, 7));
  TEST_ASSERT_EQUAL_HEX32(0x111111u, c.getPixel(44, 7));
  TEST_ASSERT_EQUAL_HEX32(0u, c.getPixel(45, 7));
}

static void test_timemode5_big_clock_centers_on_wide_matrix() {
  Settings s;
  s.timeSeparatorMode = kSepBlink;
  s.timeMode = 5;
  s.timeColor = OptColor{0x00FF00u, true};
  RuntimeState rt;
  RenderCtx ctx = ctxFor(s, rt);
  ctx.hour = 11; ctx.minute = 11; ctx.second = 0;
  Canvas c(64, 8);
  TimeApp app;
  app.render(c, ctx);
  TEST_ASSERT_EQUAL_HEX32(0u, c.getPixel(15, 3));
  TEST_ASSERT_EQUAL_HEX32(0u, c.getPixel(48, 3));
  TEST_ASSERT_EQUAL_HEX32(0x000000u, c.getPixel(16, 0));
  TEST_ASSERT_EQUAL_HEX32(0x00FF00u, c.getPixel(18, 0));
  TEST_ASSERT_EQUAL_HEX32(0x000000u, c.getPixel(22, 3));
  TEST_ASSERT_EQUAL_HEX32(0x00FF00u, c.getPixel(31, 1));
}

static void test_timemode6_binary_clock_centers_on_wide_matrix() {
  Settings s;
  s.timeMode = 6;
  RuntimeState rt;
  RenderCtx ctx = ctxFor(s, rt);
  ctx.hour = 1;
  ctx.minute = 0;
  ctx.second = 0;
  Canvas c(64, 8);
  TimeApp app;
  app.render(c, ctx);
  TEST_ASSERT_EQUAL_HEX32(0u, c.getPixel(20, 0));
  TEST_ASSERT_EQUAL_HEX32(0xFFFFFFu, c.getPixel(21, 0));
  TEST_ASSERT_EQUAL_HEX32(0xFF0000u, c.getPixel(41, 0));
  TEST_ASSERT_EQUAL_HEX32(0xFFFFFFu, c.getPixel(41, 3));
  TEST_ASSERT_EQUAL_HEX32(0u, c.getPixel(43, 0));
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

static void test_calendar_box_centers_on_wide_matrix() {
  Settings s;
  s.timeMode = 1;
  s.calendarBodyColor = 0x222222u;
  s.calendarHeaderColor = 0xFF0000u;
  s.weekdayBar.show = false;
  RuntimeState rt;
  RenderCtx ctx = ctxFor(s, rt);
  ctx.mday = 15;
  Canvas c(64, 8);
  TimeApp app;
  app.render(c, ctx);
  TEST_ASSERT_EQUAL_HEX32(0u, c.getPixel(15, 0));
  TEST_ASSERT_EQUAL_HEX32(0xFF0000u, c.getPixel(16, 0));
  TEST_ASSERT_EQUAL_HEX32(0x222222u, c.getPixel(16, 5));
  TEST_ASSERT_EQUAL_HEX32(0u, c.getPixel(25, 5));
}

static void test_sensor_apps_draw_v3_icons() {
  Settings s;
  RuntimeState rt;
  RenderCtx ctx = ctxFor(s, rt);

  Canvas c(32, 8);
  TempApp temp;
  temp.render(c, ctx);
  TEST_ASSERT_NOT_EQUAL(0u, c.getPixel(1, 0));
  TEST_ASSERT_NOT_EQUAL(0u, c.getPixel(2, 2));
  TEST_ASSERT_EQUAL_HEX32(0u, c.getPixel(7, 0));

  c.clear(0u);
  HumidityApp hum;
  hum.render(c, ctx);
  TEST_ASSERT_EQUAL_HEX32(0u, c.getPixel(0, 0));
  TEST_ASSERT_NOT_EQUAL(0u, c.getPixel(3, 0));
  TEST_ASSERT_NOT_EQUAL(0u, c.getPixel(3, 7));
  TEST_ASSERT_EQUAL_HEX32(0u, c.getPixel(6, 0));
}

static void test_battery_gauge_fills_with_charge() {
  Settings s;
  RuntimeState rt;
  RenderCtx ctx = ctxFor(s, rt);
  Canvas c(32, 8);
  BatteryApp bat;

  rt.batteryPercent = 0;
  bat.render(c, ctx);
  TEST_ASSERT_EQUAL_STRING("Battery", bat.id().c_str());
  TEST_ASSERT_NOT_EQUAL(0u, c.getPixel(2, 0));
  TEST_ASSERT_NOT_EQUAL(0u, c.getPixel(1, 4));
  TEST_ASSERT_NOT_EQUAL(0u, c.getPixel(5, 4));
  TEST_ASSERT_NOT_EQUAL(0u, c.getPixel(3, 7));
  TEST_ASSERT_EQUAL_HEX32(0u, c.getPixel(0, 0));
  TEST_ASSERT_EQUAL_HEX32(0u, c.getPixel(6, 4));
  TEST_ASSERT_EQUAL_HEX32(0u, c.getPixel(3, 6));
  TEST_ASSERT_EQUAL_HEX32(0u, c.getPixel(3, 1));

  c.clear(0u);
  rt.batteryPercent = 100;
  bat.render(c, ctx);
  for (int x = 2; x <= 4; ++x)
    for (int y = 2; y <= 6; ++y) TEST_ASSERT_EQUAL_HEX32(0x00E000u, c.getPixel(x, y));
  TEST_ASSERT_EQUAL_HEX32(0x00E000u, c.getPixel(3, 1));

  c.clear(0u);
  rt.batteryPercent = 50;
  bat.render(c, ctx);
  TEST_ASSERT_EQUAL_HEX32(0x00E000u, c.getPixel(3, 6));
  TEST_ASSERT_EQUAL_HEX32(0x00E000u, c.getPixel(3, 4));
  TEST_ASSERT_EQUAL_HEX32(0u, c.getPixel(3, 3));
  TEST_ASSERT_EQUAL_HEX32(0u, c.getPixel(3, 2));
  TEST_ASSERT_EQUAL_HEX32(0u, c.getPixel(3, 1));

  c.clear(0u);
  rt.batteryPercent = 60;
  bat.render(c, ctx);
  const uint32_t partial = c.getPixel(3, 3);
  TEST_ASSERT_TRUE(partial != 0u && partial != 0x00E000u);
  TEST_ASSERT_EQUAL_HEX32(0u, c.getPixel(3, 2));

  c.clear(0u);
  rt.batteryPercent = 1;
  bat.render(c, ctx);
  const uint32_t lastDrop = c.getPixel(3, 6);
  const uint32_t wall = c.getPixel(1, 4);
  TEST_ASSERT_NOT_EQUAL(0u, lastDrop);
  TEST_ASSERT_EQUAL_HEX32(0u, c.getPixel(3, 5));
  TEST_ASSERT_TRUE(color::red(lastDrop) + color::green(lastDrop) + color::blue(lastDrop) >
                   color::red(wall) + color::green(wall) + color::blue(wall));
}

static void test_battery_gauge_colours_by_level() {
  Settings s;
  RuntimeState rt;
  RenderCtx ctx = ctxFor(s, rt);
  Canvas c(32, 8);
  BatteryApp bat;

  rt.batteryPercent = 18;
  bat.render(c, ctx);
  TEST_ASSERT_EQUAL_HEX32(0xFF2000u, c.getPixel(3, 6));

  c.clear(0u);
  rt.batteryPercent = 30;
  bat.render(c, ctx);
  TEST_ASSERT_EQUAL_HEX32(0xFFA000u, c.getPixel(3, 6));

  c.clear(0u);
  rt.batteryPercent = 80;
  bat.render(c, ctx);
  TEST_ASSERT_EQUAL_HEX32(0x00E000u, c.getPixel(3, 6));

  c.clear(0u);
  rt.lowBattery = true;
  bat.render(c, ctx);
  TEST_ASSERT_EQUAL_HEX32(0xFF2000u, c.getPixel(3, 6));
  TEST_ASSERT_EQUAL_HEX32(0x4C0900u, c.getPixel(1, 4));
}

static void test_provisioning_screen() {
  Canvas c(32, 8);
  render::drawProvisioningScreen(c, kFont, 0);
  const uint32_t at0 = c.getPixel(2, 6);
  TEST_ASSERT_NOT_EQUAL(0u, at0);
  render::drawProvisioningScreen(c, kFont, 0);
  TEST_ASSERT_EQUAL_HEX32(at0, c.getPixel(2, 6));
  render::drawProvisioningScreen(c, kFont, 2560);
  TEST_ASSERT_NOT_EQUAL(at0, c.getPixel(2, 6));
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

static const int kStarCeiling = 70;
static const int kStarBudget = 18;

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

static void test_boot_logo_assembles_from_below() {
  Canvas c(32, 8);
  render::drawBootLogo(c, bootFont(), 0, 0);
  TEST_ASSERT_TRUE(litCount(c) <= kStarBudget);

  render::drawBootLogo(c, bootFont(), 0, render::kBootIntroMs / 3);
  TEST_ASSERT_TRUE(litCount(c) > kStarBudget);
}

static void test_boot_ng_stands_full_height() {
  Canvas c(32, 8);
  render::drawBootLogo(c, bootFont(), 0, render::kBootIntroMs);
  bool m[256];
  brightMask(c, m);
  int inTop = 0, inBottom = 0;
  for (int x = 0; x < 32; ++x) {
    if (m[x]) ++inTop;
    if (m[7 * 32 + x]) ++inBottom;
  }
  TEST_ASSERT_TRUE(inTop > 0);
  TEST_ASSERT_TRUE(inBottom > 0);
}

static void test_boot_logo_holds_still_once_assembled() {
  Canvas a(32, 8), b(32, 8);
  render::drawBootLogo(a, bootFont(), 0, render::kBootIntroMs);
  render::drawBootLogo(b, bootFont(), 0, render::kBootIntroMs + 500);
  bool ma[256], mb[256];
  brightMask(a, ma);
  brightMask(b, mb);
  for (int i = 0; i < 256; ++i) TEST_ASSERT_EQUAL(ma[i], mb[i]);
}

static void test_boot_logo_gradient_keeps_moving() {
  Canvas c(32, 8);
  render::drawBootLogo(c, bootFont(), 0, render::kBootIntroMs);
  bool m[256];
  brightMask(c, m);
  int px = -1, py = -1;
  for (int i = 0; i < 256 && px < 0; ++i)
    if (m[i]) { px = i % 32; py = i / 32; }
  TEST_ASSERT_TRUE(px >= 0);

  const uint32_t at0 = c.getPixel(px, py);
  render::drawBootLogo(c, bootFont(), 0, render::kBootIntroMs + 1500);
  TEST_ASSERT_NOT_EQUAL(at0, c.getPixel(px, py));
}

static void test_boot_stars_twinkle_behind_the_logo() {
  Canvas base(32, 8);
  render::drawBootLogo(base, bootFont(), 0, render::kBootIntroMs);
  bool logo[256];
  brightMask(base, logo);
  bool changed = false;
  for (int step = 1; step < 60 && !changed; ++step) {
    Canvas b(32, 8);
    render::drawBootLogo(b, bootFont(), 0, render::kBootIntroMs + 50 * step);
    for (int y = 0; y < 8 && !changed; ++y)
      for (int x = 0; x < 32 && !changed; ++x)
        if (!logo[y * 32 + x] && base.getPixel(x, y) != b.getPixel(x, y)) changed = true;
  }
  TEST_ASSERT_TRUE(changed);
}

static void test_boot_address_starts_where_the_logo_rests() {
  Canvas logo(32, 8);
  render::drawBootLogo(logo, bootFont(), 0, render::kBootIntroMs);
  Canvas addr(32, 8);
  render::drawBootAddress(addr, bootFont(), "1.2.3.4", render::kBootIntroMs, render::kBootIntroMs);
  for (int y = 0; y < 8; ++y)
    for (int x = 0; x < 32; ++x)
      TEST_ASSERT_EQUAL_HEX32(logo.getPixel(x, y), addr.getPixel(x, y));
}

static void test_boot_address_runs_off_the_display() {
  Canvas c(32, 8);
  TEST_ASSERT_TRUE(render::drawBootAddress(c, bootFont(), "1.2.3.4", 0, 0));
  int64_t t = 0;
  while (t < 30000 && render::drawBootAddress(c, bootFont(), "1.2.3.4", 0, t)) t += 40;
  TEST_ASSERT_TRUE(t < 30000);
  TEST_ASSERT_TRUE(litCount(c) <= kStarBudget);
}

static void test_weekday_bar() {
  Settings s;
  s.timeMode = 0;
  s.weekdayBar.show = true;
  s.weekdayBar.startOnMonday = false;
  s.weekdayBar.weekendMask = 0;
  s.weekdayBar.activeColor = 0xFF0000u;
  s.weekdayBar.inactiveColor = 0x111111u;
  RuntimeState rt;
  RenderCtx ctx = ctxFor(s, rt);
  ctx.weekday = 0;
  Canvas c(32, 8);
  TimeApp app;
  app.render(c, ctx);
  TEST_ASSERT_EQUAL_HEX32(0xFF0000u, c.getPixel(2, 7));
  TEST_ASSERT_EQUAL_HEX32(0x111111u, c.getPixel(6, 7));
}

static void test_timemode_calendar_box() {
  Settings s;
  s.timeMode = 1;
  s.calendarBodyColor = 0x222222u;
  s.calendarHeaderColor = 0xFF0000u;
  s.weekdayBar.show = false;
  RuntimeState rt;
  RenderCtx ctx = ctxFor(s, rt);
  ctx.mday = 15;
  Canvas c(32, 8);
  TimeApp app;
  app.render(c, ctx);
  TEST_ASSERT_EQUAL_HEX32(0xFF0000u, c.getPixel(0, 0));
  TEST_ASSERT_EQUAL_HEX32(0x222222u, c.getPixel(0, 5));
  TEST_ASSERT_NOT_EQUAL(0u, c.getPixel(11, 7));
}

static void test_dateapp_weekday_bar() {
  Settings s;
  s.weekdayBar.show = true;
  s.weekdayBar.startOnMonday = false;
  s.weekdayBar.weekendMask = 0;
  s.weekdayBar.activeColor = 0xFF0000u;
  s.weekdayBar.inactiveColor = 0x111111u;
  RuntimeState rt;
  RenderCtx ctx = ctxFor(s, rt);
  ctx.weekday = 0;
  Canvas c(32, 8);
  DateApp app;
  app.render(c, ctx);
  TEST_ASSERT_EQUAL_HEX32(0xFF0000u, c.getPixel(2, 7));
  TEST_ASSERT_EQUAL_HEX32(0x111111u, c.getPixel(6, 7));
}

static void test_timemode5_big_clock() {
  Settings s;
  s.timeSeparatorMode = kSepBlink;
  s.timeMode = 5;
  s.timeColor = OptColor{0x00FF00u, true};
  RuntimeState rt;
  RenderCtx ctx = ctxFor(s, rt);
  ctx.hour = 11; ctx.minute = 11; ctx.second = 0;
  Canvas c(32, 8);
  TimeApp app;
  app.render(c, ctx);
  TEST_ASSERT_EQUAL_HEX32(0x000000u, c.getPixel(0, 0));
  TEST_ASSERT_EQUAL_HEX32(0x00FF00u, c.getPixel(2, 0));
  TEST_ASSERT_EQUAL_HEX32(0x000000u, c.getPixel(6, 3));
  TEST_ASSERT_EQUAL_HEX32(0x000000u, c.getPixel(15, 7));
  TEST_ASSERT_EQUAL_HEX32(0x00FF00u, c.getPixel(15, 1));
  ctx.second = 1;
  app.render(c, ctx);
  TEST_ASSERT_EQUAL_HEX32(0x000000u, c.getPixel(15, 1));
}

static void test_timemode6_binary_clock() {
  Settings s;
  s.timeMode = 6;
  RuntimeState rt;
  RenderCtx ctx = ctxFor(s, rt);
  ctx.hour = 1;
  ctx.minute = 0;
  ctx.second = 0;
  Canvas c(32, 8);
  TimeApp app;
  app.render(c, ctx);
  TEST_ASSERT_EQUAL_HEX32(0xFF0000u, c.getPixel(25, 0));
  TEST_ASSERT_EQUAL_HEX32(0xFFFFFFu, c.getPixel(5, 0));
  TEST_ASSERT_EQUAL_HEX32(0xFFFFFFu, c.getPixel(25, 3));
}

int main(int, char**) {
  UNITY_BEGIN();
  RUN_TEST(test_timemode5_big_clock);
  RUN_TEST(test_timemode6_binary_clock);
  RUN_TEST(test_weekday_bar);
  RUN_TEST(test_dateapp_weekday_bar);
  RUN_TEST(test_timemode_calendar_box);
  RUN_TEST(test_timeapp_renders_centered);
  RUN_TEST(test_timeapp_separator_blinks);
  RUN_TEST(test_timeapp_separator_pulses);
  RUN_TEST(test_timeapp_12h_no_leading_zero);
  RUN_TEST(test_dateapp_uses_date_color);
  RUN_TEST(test_weekday_bar_centers_on_wide_matrix);
  RUN_TEST(test_weekday_bar_centers_beside_calendar_box);
  RUN_TEST(test_dateapp_weekday_bar_centers_on_wide_matrix);
  RUN_TEST(test_timemode5_big_clock_centers_on_wide_matrix);
  RUN_TEST(test_timemode6_binary_clock_centers_on_wide_matrix);
  RUN_TEST(test_icon_apps_translate_to_the_centre);
  RUN_TEST(test_calendar_box_translates_to_the_centre);
  RUN_TEST(test_icon_block_grows_with_long_text);
  RUN_TEST(test_calendar_box_centers_on_wide_matrix);
  RUN_TEST(test_sensor_apps_draw_v3_icons);
  RUN_TEST(test_battery_gauge_fills_with_charge);
  RUN_TEST(test_battery_gauge_colours_by_level);
  RUN_TEST(test_provisioning_screen);
  RUN_TEST(test_boot_logo_assembles_from_below);
  RUN_TEST(test_boot_ng_stands_full_height);
  RUN_TEST(test_boot_logo_holds_still_once_assembled);
  RUN_TEST(test_boot_logo_gradient_keeps_moving);
  RUN_TEST(test_boot_stars_twinkle_behind_the_logo);
  RUN_TEST(test_boot_address_starts_where_the_logo_rests);
  RUN_TEST(test_boot_address_runs_off_the_display);
  return UNITY_END();
}
