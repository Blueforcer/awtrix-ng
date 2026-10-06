#include <unity.h>

#include "../../../test/Visuals.h"

#include <algorithm>
#include <string>

#include "media/AwtrixFontAdapter.h"
#include "core/render/Canvas.h"
#include "core/render/TextRenderer.h"
#include "platform/tc002/runtime/Tc002ClockApp.h"

using namespace awtrix;
namespace visual = awtrix::test;

void setUp() {}
void tearDown() {}

namespace {

// Wednesday 23 September 2026, 12:34:05; the month starts on a Tuesday.
struct When {
  int year = 2026, month = 9, mday = 23, weekday = 3;
  int hour = 12, minute = 34, second = 5, ms = 0;
  bool set = true;
  int64_t nowMs = 100000;
  int64_t shownSinceMs = 0;
};

RuntimeState g_runtime;

RenderCtx ctxAt(const Settings& s, const When& w) {
  RenderCtx ctx;
  ctx.settings = &s;
  ctx.runtime = &g_runtime;
  ctx.font = &awtrixFont();
  ctx.fonts = &awtrixFontCatalog();
  ctx.nowMs = w.nowMs;
  ctx.hour = w.hour;
  ctx.minute = w.minute;
  ctx.second = w.second;
  ctx.weekday = w.weekday;
  ctx.mday = w.mday;
  ctx.month = w.month;
  ctx.year = w.year;
  ctx.epochMs = w.set ? 1790000000000LL + w.ms : -1;
  ctx.shownSinceMs = w.shownSinceMs;
  return ctx;
}

Canvas draw(Tc002ClockApp& app, const Settings& s, const When& w) {
  Canvas c(52, 16);
  c.clear();
  app.render(c, ctxAt(s, w));
  return c;
}

Canvas draw(const Settings& s, const When& w) {
  Tc002ClockApp app;
  return draw(app, s, w);
}


Settings base(int face = kClockFaceSheet) {
  Settings s;
  s.timeSeparatorMode = kSepSteady;
  s.clockFace = face;
  return s;
}

Settings calendar(int face = kClockFaceSheet) {
  Settings s = base(face);
  s.timeColor = {0, true};
  s.weekdayBar.show = false;
  return s;
}

}

void test_every_clock_face_shows_changing_time() {
  for (int face = 0; face < kClockFaceCount; ++face) {
    const Settings s = base(face);
    const Canvas initial = draw(s, When{});
    TEST_ASSERT_GREATER_THAN_INT(0, visual::countPixels(initial, visual::lit));
    When hour, minute;
    hour.hour = 13;
    minute.minute = 35;
    TEST_ASSERT_FALSE(visual::sameFrame(initial, draw(s, hour)));
    TEST_ASSERT_FALSE(visual::sameFrame(initial, draw(s, minute)));
  }
}

void test_flap_animates_then_settles_on_the_new_minute() {
  const Settings s = base(kClockFaceFlap);
  Tc002ClockApp app;
  When before;
  before.minute = 33;
  before.second = 59;
  before.ms = 900;
  before.nowMs = 99000;
  const Canvas old = draw(app, s, before);
  When next;
  next.second = 0;
  next.ms = 150;
  next.nowMs = 99250;
  const Canvas expected = draw(s, next);
  const Canvas folding = draw(app, s, next);
  TEST_ASSERT_FALSE(visual::sameFrame(old, expected));
  TEST_ASSERT_FALSE(visual::sameFrame(folding, old));
  TEST_ASSERT_FALSE(visual::sameFrame(folding, expected));
  next.ms = 450;
  next.nowMs += 300;
  TEST_ASSERT_FALSE(visual::sameFrame(draw(app, s, next), expected));
  next.ms = 650;
  next.nowMs += 200;
  TEST_ASSERT_TRUE(visual::sameFrame(draw(app, s, next), expected));
}

void test_flap_uses_the_hour_it_previously_showed() {
  const Settings s = base(kClockFaceFlap);
  When before;
  before.hour = 2;
  before.minute = 59;
  before.second = 59;
  before.ms = 900;
  before.nowMs = 50000;
  When other = before;
  other.hour = 1;
  Tc002ClockApp a, b;
  draw(a, s, before);
  draw(b, s, other);
  When after = before;
  after.hour = 3;
  after.minute = 0;
  after.second = 0;
  after.ms = 150;
  after.nowMs = 50250;
  TEST_ASSERT_FALSE(visual::sameFrame(draw(a, s, after), draw(b, s, after)));
  after.ms = 750;
  after.nowMs += 600;
  TEST_ASSERT_TRUE(visual::sameFrame(draw(a, s, after), draw(b, s, after)));
}

void test_big_face_date_and_weekday_remain_separate() {
  Settings s = base(kClockFaceBig);
  s.timeColor = {0, true};
  s.dateColor = {0xFF0000, true};
  s.calendarHeaderColor = 0x00FF00;
  for (int order = 0; order < 3; ++order)
    for (bool names : {false, true})
      for (int year : {kYearNone, kYearTwoDigit, kYearFourDigit})
        for (int month = 1; month <= 12; ++month)
          for (int weekday = 0; weekday < 7; ++weekday) {
            s.dateOrder = order;
            s.dateMonthNames = names;
            s.dateYearMode = year;
            When w;
            w.month = month;
            w.mday = 28;
            w.weekday = weekday;
            const Canvas c = draw(s, w);
            const auto date = visual::bounds(c, visual::red);
            const auto day = visual::bounds(c, visual::green);
            TEST_ASSERT_TRUE(date.found() && day.found());
            TEST_ASSERT_TRUE(day.right < date.left);
          }
}

void test_calendar_waits_until_visible_then_tears_off_once() {
  const Settings s = calendar();
  When yesterday;
  yesterday.mday = 22;
  yesterday.weekday = 2;
  const Canvas old = draw(s, yesterday), today = draw(s, When{});
  Tc002ClockApp app;
  When w;
  w.shownSinceMs = -1;
  w.nowMs = 10500;
  TEST_ASSERT_TRUE(visual::sameFrame(draw(app, s, w), old));
  w.shownSinceMs = 11000;
  w.nowMs = 11000;
  TEST_ASSERT_TRUE(visual::sameFrame(draw(app, s, w), old));
  w.nowMs = 11550;
  const Canvas moving = draw(app, s, w);
  TEST_ASSERT_FALSE(visual::sameFrame(moving, old));
  TEST_ASSERT_FALSE(visual::sameFrame(moving, today));
  w.nowMs = 12200;
  TEST_ASSERT_TRUE(visual::sameFrame(draw(app, s, w), today));
  w.nowMs = 13000;
  TEST_ASSERT_TRUE(visual::sameFrame(draw(app, s, w), today));
  w.shownSinceMs = 14000;
  w.nowMs = 14550;
  TEST_ASSERT_FALSE(visual::sameFrame(draw(app, s, w), today));
}

void test_calendar_animates_date_changes_but_not_repeated_hours() {
  const Settings s = calendar(kClockFaceRing);
  for (int hour : {0, 1}) {
    When before;
    before.mday = 22;
    before.weekday = 2;
    before.hour = 23;
    before.minute = before.second = 59;
    before.nowMs = 200000;
    When after;
    after.hour = hour;
    after.minute = after.second = 0;
    after.nowMs = 200100;
    Tc002ClockApp app;
    draw(app, s, before);
    draw(app, s, after);
    after.nowMs += 550;
    TEST_ASSERT_FALSE(visual::sameFrame(draw(app, s, after), draw(s, after)));
    after.nowMs += 1500;
    TEST_ASSERT_TRUE(visual::sameFrame(draw(app, s, after), draw(s, after)));
    after.minute = 59;
    draw(app, s, after);
    after.minute = 0;
    after.nowMs += 100;
    TEST_ASSERT_TRUE(visual::sameFrame(draw(app, s, after), draw(s, after)));
  }
}

void test_midnight_during_an_incoming_transition_settles_once() {
  const Settings s = calendar();
  const Canvas settled = draw(s, When{});
  Tc002ClockApp app;
  When before;
  before.mday = 22;
  before.weekday = 2;
  before.hour = 23;
  before.minute = before.second = 59;
  before.shownSinceMs = -1;
  before.nowMs = 300000;
  draw(app, s, before);
  When w;
  w.hour = w.minute = w.second = 0;
  w.shownSinceMs = -1;
  w.nowMs = 300600;
  draw(app, s, w);
  w.shownSinceMs = 300900;
  w.nowMs = 300900;
  draw(app, s, w);
  w.nowMs = 301450;
  TEST_ASSERT_FALSE(visual::sameFrame(draw(app, s, w), settled));
  w.nowMs = 302100;
  TEST_ASSERT_TRUE(visual::sameFrame(draw(app, s, w), settled));
}

void test_disabling_calendar_animation_shows_each_date_immediately() {
  for (int face : {kClockFaceSheet, kClockFaceRing, kClockFaceMonth, kClockFaceFlap}) {
    Settings s = calendar(face);
    s.calendarAnimation = false;
    Tc002ClockApp app;
    When w;
    w.shownSinceMs = -1;
    w.nowMs = 10500;
    TEST_ASSERT_TRUE(visual::sameFrame(draw(app, s, w), draw(s, w)));
    w.shownSinceMs = 11000;
    w.nowMs = 11550;
    TEST_ASSERT_TRUE(visual::sameFrame(draw(app, s, w), draw(s, w)));
    ++w.mday;
    ++w.weekday;
    w.nowMs += 100;
    TEST_ASSERT_TRUE(visual::sameFrame(draw(app, s, w), draw(s, w)));
  }
}

void test_sheet_names_the_weekday_when_the_weekday_bar_is_hidden() {
  Settings s = base();
  s.weekdayBar.show = false;
  TEST_ASSERT_TRUE(visual::findText(draw(s, When{}), awtrixFont(), "WED", visual::brightNeutral).found());
  s.weekdayBar.show = true;
  TEST_ASSERT_TRUE(visual::findText(draw(s, When{}), awtrixFont(), "SEP", visual::brightNeutral).found());
}

void test_names_remain_capitals_and_big_face_shows_date_and_weekday() {
  for (int face : {int(kClockFaceSheet), int(kClockFaceBig)})
    for (bool bar : {true, false}) {
      Settings upper = base(face);
      upper.weekdayBar.show = bar;
      upper.dateMonthNames = true;
      Settings lower = upper;
      lower.uppercase = false;
      TEST_ASSERT_TRUE(visual::sameFrame(draw(upper, When{}), draw(lower, When{})));
    }
  const Settings s = base(kClockFaceBig);
  const Canvas initial = draw(s, When{});
  When day, date;
  day.weekday = 4;
  date.mday = 24;
  TEST_ASSERT_FALSE(visual::sameFrame(initial, draw(s, day)));
  TEST_ASSERT_FALSE(visual::sameFrame(initial, draw(s, date)));
}

void test_big_face_shows_seconds_only_when_enabled() {
  Settings s = base(kClockFaceBig);
  When later;
  later.second = 56;
  TEST_ASSERT_TRUE(visual::sameFrame(draw(s, When{}), draw(s, later)));
  s.timeShowSeconds = true;
  TEST_ASSERT_FALSE(visual::sameFrame(draw(s, When{}), draw(s, later)));
}

void test_unset_clock_ignores_time_and_date_values() {
  When unset;
  unset.set = false;
  When other = unset;
  other.hour = 21;
  other.minute = 5;
  other.mday = 2;
  other.weekday = 5;
  other.month = 3;
  for (int face = 0; face < kClockFaceCount; ++face) {
    const Canvas c = draw(base(face), unset);
    TEST_ASSERT_GREATER_THAN_INT(0, visual::countPixels(c, visual::lit));
    TEST_ASSERT_TRUE(visual::sameFrame(c, draw(base(face), other)));
    TEST_ASSERT_FALSE(visual::sameFrame(c, draw(base(face), When{})));
  }
}

void test_cached_time_tracks_settings_time_and_clock_availability() {
  Tc002ClockApp app;
  When w;
  w.hour = 9;
  w.second = 15;  // Keep the flap settled while comparing a persistent and a fresh app.
  for (int face = 0; face < kClockFaceCount; ++face) {
    Settings s = base(face);
    s.calendarAnimation = false;
    for (bool hours24 : {false, true})
      for (bool leading : {false, true})
        for (bool seconds : {false, true}) {
          s.time24h = hours24;
          s.timeLeadingZero = leading;
          s.timeShowSeconds = seconds;
          for (bool set : {true, false, true}) {
            w.set = set;
            TEST_ASSERT_TRUE(visual::sameFrame(draw(app, s, w), draw(s, w)));
            ++w.second;
            TEST_ASSERT_TRUE(visual::sameFrame(draw(app, s, w), draw(s, w)));
          }
          w.second = 15;
        }
  }
}

int main(int, char**) {
  UNITY_BEGIN();
  RUN_TEST(test_every_clock_face_shows_changing_time);
  RUN_TEST(test_flap_animates_then_settles_on_the_new_minute);
  RUN_TEST(test_flap_uses_the_hour_it_previously_showed);
  RUN_TEST(test_big_face_date_and_weekday_remain_separate);
  RUN_TEST(test_calendar_waits_until_visible_then_tears_off_once);
  RUN_TEST(test_calendar_animates_date_changes_but_not_repeated_hours);
  RUN_TEST(test_midnight_during_an_incoming_transition_settles_once);
  RUN_TEST(test_disabling_calendar_animation_shows_each_date_immediately);
  RUN_TEST(test_sheet_names_the_weekday_when_the_weekday_bar_is_hidden);
  RUN_TEST(test_names_remain_capitals_and_big_face_shows_date_and_weekday);
  RUN_TEST(test_big_face_shows_seconds_only_when_enabled);
  RUN_TEST(test_unset_clock_ignores_time_and_date_values);
  RUN_TEST(test_cached_time_tracks_settings_time_and_clock_availability);
  return UNITY_END();
}
