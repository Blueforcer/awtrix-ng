#include <unity.h>

#include "core/notify/NotificationManager.h"

using namespace awtrix;

void setUp() {}
void tearDown() {}

static AppSpec mk(const char* text, long durMs = 0, bool hold = false, bool stack = true) {
  AppSpec s;
  s.isNotification = true;
  s.text = text;
  s.durationMs = durMs;
  s.hold = hold;
  s.stack = stack;
  return s;
}

static void test_push_and_current() {
  NotificationManager nm(4);
  TEST_ASSERT_FALSE(nm.hasCurrent());
  nm.push(mk("a", 1000), 0);
  nm.push(mk("b", 1000), 0);
  TEST_ASSERT_EQUAL_UINT(2u, (unsigned)nm.size());
  TEST_ASSERT_EQUAL_STRING("a", nm.current().text.c_str());
}

static void test_hold_keeps_a_notification_past_its_duration() {
  NotificationManager nm(4);
  nm.push(mk("a", 1000), 0);
  nm.push(mk("b", 1000), 0);

  nm.update(5000, 5000, true);
  TEST_ASSERT_EQUAL_STRING("a", nm.current().text.c_str());
  nm.update(50000, 5000, true);
  TEST_ASSERT_EQUAL_STRING("a", nm.current().text.c_str());

  nm.update(50000, 5000, false);
  TEST_ASSERT_EQUAL_STRING("b", nm.current().text.c_str());
}

static void test_hold_does_not_shorten_the_duration() {
  NotificationManager nm(4);
  nm.push(mk("a", 1000), 0);
  nm.push(mk("b", 1000), 0);
  nm.update(500, 5000, true);
  nm.update(999, 5000, false);
  TEST_ASSERT_EQUAL_STRING("a", nm.current().text.c_str());
  nm.update(1000, 5000, false);
  TEST_ASSERT_EQUAL_STRING("b", nm.current().text.c_str());
}

static void test_finished_repeats_end_a_notification_early() {
  NotificationManager nm(4);
  nm.push(mk("a"), 0);
  nm.push(mk("b"), 0);
  nm.update(1000, 5000, false, true);
  TEST_ASSERT_EQUAL_STRING("b", nm.current().text.c_str());
}

static void test_finished_repeats_wait_for_an_explicit_duration() {
  NotificationManager nm(4);
  nm.push(mk("a", 3000), 0);
  nm.push(mk("b", 3000), 0);
  nm.update(2999, 5000, false, true);
  TEST_ASSERT_EQUAL_STRING("a", nm.current().text.c_str());
  nm.update(3000, 5000, false, true);
  TEST_ASSERT_EQUAL_STRING("b", nm.current().text.c_str());
}

static void test_hold_outranks_finished_repeats() {
  NotificationManager nm(4);
  nm.push(mk("h", 100, true), 0);
  nm.update(100000, 5000, false, true);
  TEST_ASSERT_TRUE(nm.hasCurrent());
}

static void test_update_expires_and_advances() {
  NotificationManager nm(4);
  nm.push(mk("a", 1000), 0);
  nm.push(mk("b", 1000), 0);
  nm.update(500, 5000);
  TEST_ASSERT_EQUAL_STRING("a", nm.current().text.c_str());
  nm.update(1000, 5000);
  TEST_ASSERT_EQUAL_STRING("b", nm.current().text.c_str());
  nm.update(1999, 5000);
  TEST_ASSERT_EQUAL_STRING("b", nm.current().text.c_str());
  nm.update(2000, 5000);
  TEST_ASSERT_FALSE(nm.hasCurrent());
}

static void test_hold_never_expires() {
  NotificationManager nm(4);
  nm.push(mk("h", 100, true), 0);
  nm.update(100000, 5000);
  TEST_ASSERT_TRUE(nm.hasCurrent());
}

static void test_default_duration_when_zero() {
  NotificationManager nm(4);
  nm.push(mk("a", 0), 0);
  nm.update(4999, 5000);
  TEST_ASSERT_TRUE(nm.hasCurrent());
  nm.update(5000, 5000);
  TEST_ASSERT_FALSE(nm.hasCurrent());
}

static void test_no_stack_replaces_current() {
  NotificationManager nm(4);
  nm.push(mk("a", 1000), 0);
  nm.push(mk("b", 1000), 0);
  nm.push(mk("c", 1000, false, false), 10);
  TEST_ASSERT_EQUAL_UINT(2u, (unsigned)nm.size());
  TEST_ASSERT_EQUAL_STRING("c", nm.current().text.c_str());
}

static void test_full_rejects_stacked() {
  NotificationManager nm(2);
  TEST_ASSERT_TRUE(nm.push(mk("a"), 0));
  TEST_ASSERT_TRUE(nm.push(mk("b"), 0));
  TEST_ASSERT_FALSE(nm.push(mk("c"), 0));
  TEST_ASSERT_EQUAL_UINT(2u, (unsigned)nm.size());
}

static void test_dismiss() {
  NotificationManager nm(4);
  nm.push(mk("a", 1000), 0);
  nm.push(mk("b", 1000), 0);
  nm.dismiss(50);
  TEST_ASSERT_EQUAL_STRING("b", nm.current().text.c_str());
  nm.dismiss(60);
  TEST_ASSERT_FALSE(nm.hasCurrent());
}

static void test_dismiss_named_removes_from_the_middle_without_disturbing_the_front() {
  NotificationManager q(8);
  AppSpec a, b, c;
  a.name = "first"; b.name = "queued"; c.name = "last";
  q.push(a, 0); q.push(b, 0); q.push(c, 0);
  const uint32_t gen = q.generation();
  TEST_ASSERT_TRUE(q.dismissNamed("queued", 10));
  TEST_ASSERT_EQUAL_UINT(2u, (unsigned)q.size());
  TEST_ASSERT_EQUAL_STRING("first", q.current().name.c_str());
  TEST_ASSERT_EQUAL_UINT32(gen, q.generation());
}

static void test_dismiss_named_on_the_front_advances_and_bumps_generation() {
  NotificationManager q(8);
  AppSpec a, b;
  a.name = "first"; b.name = "second";
  q.push(a, 0); q.push(b, 0);
  const uint32_t gen = q.generation();
  TEST_ASSERT_TRUE(q.dismissNamed("first", 10));
  TEST_ASSERT_EQUAL_STRING("second", q.current().name.c_str());
  TEST_ASSERT_TRUE(q.generation() != gen);
}

static void test_dismiss_named_reports_a_miss() {
  NotificationManager q(8);
  AppSpec a; a.name = "first";
  q.push(a, 0);
  TEST_ASSERT_FALSE(q.dismissNamed("never-sent", 10));
  TEST_ASSERT_FALSE(q.dismissNamed("", 10));
  TEST_ASSERT_EQUAL_UINT(1u, (unsigned)q.size());
}

int main(int, char**) {
  UNITY_BEGIN();
  RUN_TEST(test_dismiss_named_removes_from_the_middle_without_disturbing_the_front);
  RUN_TEST(test_dismiss_named_on_the_front_advances_and_bumps_generation);
  RUN_TEST(test_dismiss_named_reports_a_miss);
  RUN_TEST(test_push_and_current);
  RUN_TEST(test_hold_keeps_a_notification_past_its_duration);
  RUN_TEST(test_hold_does_not_shorten_the_duration);
  RUN_TEST(test_finished_repeats_end_a_notification_early);
  RUN_TEST(test_finished_repeats_wait_for_an_explicit_duration);
  RUN_TEST(test_hold_outranks_finished_repeats);
  RUN_TEST(test_update_expires_and_advances);
  RUN_TEST(test_hold_never_expires);
  RUN_TEST(test_default_duration_when_zero);
  RUN_TEST(test_no_stack_replaces_current);
  RUN_TEST(test_full_rejects_stacked);
  RUN_TEST(test_dismiss);
  return UNITY_END();
}
