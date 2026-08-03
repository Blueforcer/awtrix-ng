#include <unity.h>

#include "core/apps/AppHost.h"

using namespace awtrix;

void setUp() {}
void tearDown() {}

static int ph(AppPhase p) { return static_cast<int>(p); }

static void test_set_and_current() {
  AppHost h;
  h.setApps({"a", "b", "c"});
  TEST_ASSERT_EQUAL_UINT(3u, (unsigned)h.count());
  TEST_ASSERT_EQUAL_STRING("a", h.currentId().c_str());
  TEST_ASSERT_EQUAL_INT(ph(AppPhase::Fixed), ph(h.phase()));
}

static void test_auto_transition_cycle() {
  AppHost h;
  h.setApps({"a", "b", "c"});
  h.tick(100, 1000, 200, true);
  TEST_ASSERT_EQUAL_INT(ph(AppPhase::Fixed), ph(h.phase()));
  h.tick(1100, 1000, 200, true);
  TEST_ASSERT_EQUAL_INT(ph(AppPhase::InTransition), ph(h.phase()));
  TEST_ASSERT_EQUAL_INT(1, h.transitionTarget());
  h.tick(1200, 1000, 200, true);
  TEST_ASSERT_EQUAL_INT(ph(AppPhase::InTransition), ph(h.phase()));
  h.tick(1300, 1000, 200, true);
  TEST_ASSERT_EQUAL_STRING("b", h.currentId().c_str());
  TEST_ASSERT_EQUAL_INT(ph(AppPhase::Fixed), ph(h.phase()));
}

static void test_first_dwell_starts_at_the_first_tick() {
  AppHost h;
  h.setApps({"a", "b", "c"});
  h.tick(30000, 1000, 200, true);
  TEST_ASSERT_EQUAL_INT(ph(AppPhase::Fixed), ph(h.phase()));
  h.tick(30900, 1000, 200, true);
  TEST_ASSERT_EQUAL_INT(ph(AppPhase::Fixed), ph(h.phase()));
  TEST_ASSERT_EQUAL_STRING("a", h.currentId().c_str());
  h.tick(31000, 1000, 200, true);
  TEST_ASSERT_EQUAL_INT(ph(AppPhase::InTransition), ph(h.phase()));
}

static void test_first_dwell_respects_an_earlier_manual_switch() {
  AppHost h;
  h.setApps({"a", "b", "c"});
  h.switchTo("b", 30000);
  h.tick(30900, 1000, 200, true);
  TEST_ASSERT_EQUAL_INT(ph(AppPhase::Fixed), ph(h.phase()));
  h.tick(31000, 1000, 200, true);
  TEST_ASSERT_EQUAL_INT(ph(AppPhase::InTransition), ph(h.phase()));
}

static void test_auto_transition_off() {
  AppHost h;
  h.setApps({"a", "b"});
  h.tick(100000, 1000, 200, false);
  TEST_ASSERT_EQUAL_STRING("a", h.currentId().c_str());
  TEST_ASSERT_EQUAL_INT(ph(AppPhase::Fixed), ph(h.phase()));
}

static void test_next_wraps() {
  AppHost h;
  h.setApps({"a", "b", "c"});
  h.next(0);
  h.tick(200, 1000, 200, true);
  TEST_ASSERT_EQUAL_STRING("b", h.currentId().c_str());
  h.switchTo("c", 0);
  h.next(0);
  h.tick(200, 1000, 200, true);
  TEST_ASSERT_EQUAL_STRING("a", h.currentId().c_str());
}

static void test_previous_wraps() {
  AppHost h;
  h.setApps({"a", "b", "c"});
  h.previous(0);
  h.tick(200, 1000, 200, true);
  TEST_ASSERT_EQUAL_STRING("c", h.currentId().c_str());
}

static void test_switch_instant() {
  AppHost h;
  h.setApps({"a", "b", "c"});
  TEST_ASSERT_TRUE(h.switchTo("c", 10));
  TEST_ASSERT_EQUAL_STRING("c", h.currentId().c_str());
  TEST_ASSERT_EQUAL_INT(ph(AppPhase::Fixed), ph(h.phase()));
  TEST_ASSERT_FALSE(h.switchTo("zzz", 10));
  TEST_ASSERT_EQUAL_STRING("c", h.currentId().c_str());
}

static void test_setapps_preserves_current() {
  AppHost h;
  h.setApps({"a", "b", "c"});
  h.switchTo("b", 0);
  h.setApps({"x", "b", "y", "z"});
  TEST_ASSERT_EQUAL_STRING("b", h.currentId().c_str());
}

static void test_setapps_clamps_when_shrunk() {
  AppHost h;
  h.setApps({"a", "b", "c", "d"});
  h.switchTo("d", 0);
  h.setApps({"a", "b"});
  TEST_ASSERT_TRUE(h.currentIndex() >= 0 && h.currentIndex() < 2);
}

static void test_setapps_with_an_identical_list_changes_nothing() {
  AppHost h;
  h.setApps({"a", "b", "c"});
  h.tick(0, 1000, 200, true);
  h.tick(1000, 1000, 200, true);
  TEST_ASSERT_EQUAL_INT(ph(AppPhase::InTransition), ph(h.phase()));
  h.setApps({"a", "b", "c"});
  TEST_ASSERT_EQUAL_INT(ph(AppPhase::InTransition), ph(h.phase()));
  TEST_ASSERT_EQUAL_INT(1, h.transitionTarget());
  TEST_ASSERT_EQUAL_INT64(1000, h.phaseStartMs());
  h.tick(1200, 1000, 200, true);
  TEST_ASSERT_EQUAL_STRING("b", h.currentId().c_str());
}

static void test_setapps_keeps_a_transition_whose_endpoints_survive() {
  AppHost h;
  h.setApps({"a", "b", "c"});
  h.next(0);
  TEST_ASSERT_EQUAL_INT(1, h.transitionTarget());
  h.setApps({"z", "a", "b", "c"});
  TEST_ASSERT_EQUAL_INT(ph(AppPhase::InTransition), ph(h.phase()));
  TEST_ASSERT_EQUAL_INT(1, h.currentIndex());
  TEST_ASSERT_EQUAL_INT(2, h.transitionTarget());
  h.tick(200, 1000, 200, true);
  TEST_ASSERT_EQUAL_STRING("b", h.currentId().c_str());
}

static void test_setapps_cancels_a_transition_whose_target_left() {
  AppHost h;
  h.setApps({"a", "b", "c"});
  h.next(0);
  h.setApps({"a", "c"});
  TEST_ASSERT_EQUAL_INT(ph(AppPhase::Fixed), ph(h.phase()));
  TEST_ASSERT_EQUAL_INT(-1, h.transitionTarget());
  TEST_ASSERT_EQUAL_STRING("a", h.currentId().c_str());
}

static void test_setapps_cancels_a_transition_whose_source_left() {
  AppHost h;
  h.setApps({"a", "b", "c"});
  h.next(0);
  h.setApps({"b", "c"});
  TEST_ASSERT_EQUAL_INT(ph(AppPhase::Fixed), ph(h.phase()));
  TEST_ASSERT_EQUAL_INT(-1, h.transitionTarget());
}

static void test_a_rebuild_per_frame_still_rotates() {
  AppHost h;
  h.setApps({"a", "b", "c"});
  for (int64_t t = 0; t <= 2600; t += 50) {
    h.setApps({"a", "b", "c"});
    h.tick(t, 1000, 200, true);
  }
  TEST_ASSERT_EQUAL_STRING("c", h.currentId().c_str());
}

static void test_empty_and_single_are_safe() {
  AppHost e;
  e.setApps({});
  TEST_ASSERT_TRUE(e.empty());
  TEST_ASSERT_EQUAL_STRING("", e.currentId().c_str());
  e.tick(1000, 1000, 200, true);
  e.next(0);
  AppHost s;
  s.setApps({"only"});
  s.tick(100000, 1000, 200, true);
  TEST_ASSERT_EQUAL_STRING("only", s.currentId().c_str());
  TEST_ASSERT_EQUAL_INT(ph(AppPhase::Fixed), ph(s.phase()));
}

static void test_next_during_a_transition_advances_one_further() {
  AppHost h;
  h.setApps({"a", "b", "c", "d"});
  h.next(0);
  TEST_ASSERT_EQUAL_INT(1, h.transitionTarget());
  h.next(100);
  TEST_ASSERT_EQUAL_INT(1, h.currentIndex());
  TEST_ASSERT_EQUAL_INT(2, h.transitionTarget());
  TEST_ASSERT_EQUAL_INT64(100, h.phaseStartMs());
  h.next(150);
  TEST_ASSERT_EQUAL_INT(2, h.currentIndex());
  TEST_ASSERT_EQUAL_INT(3, h.transitionTarget());
  h.tick(350, 1000, 200, true);
  TEST_ASSERT_EQUAL_STRING("d", h.currentId().c_str());
}

static void test_a_press_per_frame_still_walks_the_whole_list() {
  AppHost h;
  h.setApps({"a", "b", "c", "d"});
  for (int64_t t = 0; t < 3 * 50; t += 50) h.next(t);
  h.tick(1000, 1000, 200, true);
  TEST_ASSERT_EQUAL_STRING("d", h.currentId().c_str());
}

static void test_previous_during_a_transition_reverses_from_the_target() {
  AppHost h;
  h.setApps({"a", "b", "c"});
  h.next(0);
  TEST_ASSERT_EQUAL_INT(1, h.transitionTarget());
  h.previous(100);
  TEST_ASSERT_EQUAL_INT(1, h.currentIndex());
  TEST_ASSERT_EQUAL_INT(0, h.transitionTarget());
  TEST_ASSERT_EQUAL_INT(-1, h.direction());
  h.tick(300, 1000, 200, true);
  TEST_ASSERT_EQUAL_STRING("a", h.currentId().c_str());
}

static void test_named_switch_during_a_transition_starts_from_the_target() {
  AppHost h;
  h.setApps({"a", "b", "c"});
  h.next(0);
  TEST_ASSERT_TRUE(h.transitionTo("c", 100));
  TEST_ASSERT_EQUAL_INT(1, h.currentIndex());
  TEST_ASSERT_EQUAL_INT(2, h.transitionTarget());
  h.tick(300, 1000, 200, true);
  TEST_ASSERT_EQUAL_STRING("c", h.currentId().c_str());
}

static void test_named_switch_to_the_running_target_lets_it_finish() {
  AppHost h;
  h.setApps({"a", "b", "c"});
  h.next(0);
  TEST_ASSERT_TRUE(h.transitionTo("b", 100));
  TEST_ASSERT_EQUAL_INT(ph(AppPhase::InTransition), ph(h.phase()));
  TEST_ASSERT_EQUAL_INT(0, h.currentIndex());
  TEST_ASSERT_EQUAL_INT64(0, h.phaseStartMs());
}

static void test_a_press_when_only_the_current_app_may_show_holds_it() {
  AppHost h;
  h.setApps({"a", "b", "c"});
  h.next(0);
  h.setShowGate([](const std::string& id) { return id == "b"; });
  h.next(100);
  TEST_ASSERT_EQUAL_INT(ph(AppPhase::Fixed), ph(h.phase()));
  TEST_ASSERT_EQUAL_STRING("b", h.currentId().c_str());
}

static void test_gate_skips_a_single_app() {
  AppHost h;
  h.setApps({"a", "b", "c"});
  h.setShowGate([](const std::string& id) { return id != "b"; });
  h.next(0);
  TEST_ASSERT_EQUAL_INT(2, h.transitionTarget());
  h.tick(200, 1000, 200, true);
  TEST_ASSERT_EQUAL_STRING("c", h.currentId().c_str());
}

static void test_gate_skips_a_run_of_apps() {
  AppHost h;
  h.setApps({"a", "b", "c", "d"});
  h.setShowGate([](const std::string& id) { return id == "a" || id == "d"; });
  h.tick(0, 1000, 200, true);
  h.tick(1000, 1000, 200, true);
  TEST_ASSERT_EQUAL_INT(3, h.transitionTarget());
  h.tick(1200, 1000, 200, true);
  TEST_ASSERT_EQUAL_STRING("d", h.currentId().c_str());
}

static void test_gate_applies_to_previous_too() {
  AppHost h;
  h.setApps({"a", "b", "c"});
  h.setShowGate([](const std::string& id) { return id != "c"; });
  h.previous(0);
  TEST_ASSERT_EQUAL_INT(-1, h.direction());
  TEST_ASSERT_EQUAL_INT(1, h.transitionTarget());
  h.tick(200, 1000, 200, true);
  TEST_ASSERT_EQUAL_STRING("b", h.currentId().c_str());
}

static void test_gate_refusing_everyone_holds_the_current_app() {
  AppHost h;
  int asked = 0;
  h.setApps({"a", "b", "c"});
  h.setShowGate([&](const std::string&) {
    ++asked;
    return false;
  });
  h.tick(0, 1000, 200, true);
  h.tick(1000, 1000, 200, true);
  TEST_ASSERT_EQUAL_STRING("a", h.currentId().c_str());
  TEST_ASSERT_EQUAL_INT(ph(AppPhase::Fixed), ph(h.phase()));
  TEST_ASSERT_EQUAL_INT(2, asked);

  h.tick(1100, 1000, 200, true);
  TEST_ASSERT_EQUAL_INT(2, asked);
  h.tick(2000, 1000, 200, true);
  TEST_ASSERT_EQUAL_INT(4, asked);
}

static void test_gate_does_not_veto_a_named_destination() {
  AppHost h;
  h.setApps({"a", "b", "c"});
  h.setShowGate([](const std::string& id) { return id == "a"; });
  TEST_ASSERT_TRUE(h.switchTo("c", 0));
  TEST_ASSERT_EQUAL_STRING("c", h.currentId().c_str());
  TEST_ASSERT_TRUE(h.transitionTo("b", 10));
  h.tick(300, 1000, 200, true);
  TEST_ASSERT_EQUAL_STRING("b", h.currentId().c_str());
}

int main(int, char**) {
  UNITY_BEGIN();
  RUN_TEST(test_set_and_current);
  RUN_TEST(test_auto_transition_cycle);
  RUN_TEST(test_first_dwell_starts_at_the_first_tick);
  RUN_TEST(test_first_dwell_respects_an_earlier_manual_switch);
  RUN_TEST(test_auto_transition_off);
  RUN_TEST(test_next_wraps);
  RUN_TEST(test_previous_wraps);
  RUN_TEST(test_switch_instant);
  RUN_TEST(test_setapps_preserves_current);
  RUN_TEST(test_setapps_clamps_when_shrunk);
  RUN_TEST(test_setapps_with_an_identical_list_changes_nothing);
  RUN_TEST(test_setapps_keeps_a_transition_whose_endpoints_survive);
  RUN_TEST(test_setapps_cancels_a_transition_whose_target_left);
  RUN_TEST(test_setapps_cancels_a_transition_whose_source_left);
  RUN_TEST(test_a_rebuild_per_frame_still_rotates);
  RUN_TEST(test_empty_and_single_are_safe);
  RUN_TEST(test_next_during_a_transition_advances_one_further);
  RUN_TEST(test_a_press_per_frame_still_walks_the_whole_list);
  RUN_TEST(test_previous_during_a_transition_reverses_from_the_target);
  RUN_TEST(test_named_switch_during_a_transition_starts_from_the_target);
  RUN_TEST(test_named_switch_to_the_running_target_lets_it_finish);
  RUN_TEST(test_a_press_when_only_the_current_app_may_show_holds_it);
  RUN_TEST(test_gate_skips_a_single_app);
  RUN_TEST(test_gate_skips_a_run_of_apps);
  RUN_TEST(test_gate_applies_to_previous_too);
  RUN_TEST(test_gate_refusing_everyone_holds_the_current_app);
  RUN_TEST(test_gate_does_not_veto_a_named_destination);
  return UNITY_END();
}
