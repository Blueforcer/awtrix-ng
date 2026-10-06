#include <unity.h>

#include <string>

#include "../../../test/Visuals.h"
#include "media/AwtrixFontAdapter.h"
#include "platform/tc002/runtime/Tc002StatusApp.h"

using namespace awtrix;
namespace visual = awtrix::test;

void setUp() {}
void tearDown() {}

namespace {

constexpr int64_t kQuiet = 6000;

RuntimeState online(const std::string& ip = "192.0.2.63", int rssi = -60) {
  RuntimeState rt;
  rt.hasBattery = true;
  rt.batteryPercent = 87;
  rt.wifi.phase = net::LinkPhase::Connected;
  rt.wifi.endpoint = ip;
  rt.wifiRssi = rssi;
  return rt;
}

struct Rig {
  Settings settings;
  RuntimeState rt;
  Tc002StatusApp app;
  int64_t last = 0;
  explicit Rig(const RuntimeState& start) : rt(start) {}
  Canvas frame(int64_t now) {
    Canvas c(52, 16);
    c.clear();
    RenderCtx ctx;
    ctx.settings = &settings;
    ctx.runtime = &rt;
    ctx.font = &awtrixFont();
    ctx.fonts = &awtrixFontCatalog();
    ctx.nowMs = now;
    app.render(c, ctx);
    last = now;
    return c;
  }
  Canvas until(int64_t now) {
    for (int64_t t = last + 24; t < now; t += 24) frame(t);
    return frame(now);
  }
};

Canvas draw(const RuntimeState& rt, int64_t now = kQuiet) { return Rig(rt).frame(now); }
bool shows(const Canvas& frame, const std::string& label) {
  return visual::findText(frame, awtrixFont(), label).found();
}

void test_low_battery_blinks_but_keeps_its_percentage_readable() {
  auto rt = online();
  rt.batteryPercent = 12;
  rt.lowBattery = true;
  const Canvas on = draw(rt, 0), off = draw(rt, 500);
  TEST_ASSERT_TRUE(visual::countPixels(on, visual::red) > visual::countPixels(off, visual::red));
  TEST_ASSERT_TRUE(shows(on, "12%") && shows(off, "12%"));
  rt.externalPower = true;
  TEST_ASSERT_EQUAL_INT(visual::countPixels(draw(rt, 1000), visual::red),
                        visual::countPixels(draw(rt, 1500), visual::red));
}

void test_unknown_battery_has_a_placeholder_and_no_charge_fill() {
  auto rt = online();
  rt.hasBattery = false;
  const Canvas frame = draw(rt);
  TEST_ASSERT_TRUE(shows(frame, "--%"));
  TEST_ASSERT_EQUAL_INT(0, visual::countPixels(frame, visual::green));
}

void test_signal_strength_grows_with_reception() {
  int previous = -1;
  for (int rssi : {0, -90, -70, -60, -50}) {
    const int bright = visual::countPixels(draw(online("192.0.2.63", rssi)), visual::brightNeutral);
    TEST_ASSERT_GREATER_THAN_INT(previous, bright);
    previous = bright;
  }
}

void test_a_wide_address_remains_readable() {
  const Canvas frame = draw(online("192.168.100.200"));
  for (const char* octet : {"192", "168", "100", "200"}) TEST_ASSERT_TRUE(shows(frame, octet));
}

void test_connecting_and_offline_replace_the_address() {
  auto rt = online("fe80::1");
  TEST_ASSERT_TRUE(shows(draw(rt), "CONNECTING"));
  rt.wifi.phase = net::LinkPhase::Connecting;
  rt.wifi.endpoint.clear();
  TEST_ASSERT_TRUE(visual::findText(draw(rt), awtrixFont(), "CONNECTING", visual::amber).found());
  rt.wifi.phase = net::LinkPhase::Offline;
  TEST_ASSERT_TRUE(visual::findText(draw(rt), awtrixFont(), "NO WIFI", visual::red).found());
  rt.wifi.phase = net::LinkPhase::Connected;
  TEST_ASSERT_TRUE(shows(draw(rt), "CONNECTING"));
}

void test_battery_changes_animate_and_settle_without_covering_the_address() {
  for (const auto& change : {std::pair<int, int>{50, 60}, {80, 30}}) {
    auto rt = online();
    rt.batteryPercent = change.first;
    Rig rig(rt);
    const Canvas before = rig.until(kQuiet);
    rig.rt.batteryPercent = change.second;
    bool animated = false;
    for (int64_t now = kQuiet + 24; now < kQuiet + 600; now += 24) {
      const Canvas frame = rig.until(now);
      animated |= !visual::sameFrame(frame, before) && !visual::sameFrame(frame, draw(rig.rt, now));
      TEST_ASSERT_TRUE(shows(frame, "192"));
    }
    TEST_ASSERT_TRUE(animated);
    const Canvas settled = rig.until(kQuiet + 1500);
    TEST_ASSERT_TRUE(shows(settled, std::to_string(change.second) + "%"));
    TEST_ASSERT_TRUE(visual::sameFrame(settled, draw(rig.rt, kQuiet + 1500)));
  }
}

void test_power_indication_is_independent_of_battery_readings() {
  for (bool known : {false, true}) {
    auto rt = online();
    rt.hasBattery = known;
    const Canvas unplugged = draw(rt);
    rt.externalPower = true;
    const Canvas powered = draw(rt);
    TEST_ASSERT_TRUE(shows(unplugged, known ? "87%" : "--%"));
    TEST_ASSERT_TRUE(shows(powered, known ? "87%" : "--%"));
    TEST_ASSERT_TRUE(visual::countPixels(powered, visual::amber) > visual::countPixels(unplugged, visual::amber));
  }
}

void test_power_changes_animate_and_settle_without_covering_the_address() {
  Rig rig(online());
  rig.until(kQuiet);
  for (bool powered : {true, false}) {
    const int64_t start = rig.last;
    rig.rt.externalPower = powered;
    bool animated = false;
    for (int64_t now = start + 24; now < start + 600; now += 24) {
      const Canvas frame = rig.until(now);
      animated |= !visual::sameFrame(frame, draw(rig.rt, now));
      TEST_ASSERT_TRUE(shows(frame, "192"));
    }
    TEST_ASSERT_TRUE(animated);
    TEST_ASSERT_TRUE(visual::sameShape(rig.until(start + 1500), draw(rig.rt, start + 1500)));
  }
}

void test_charging_animates_the_battery() {
  auto rt = online();
  const int calm = visual::countPixels(draw(rt, kQuiet + 1200), visual::green);
  rt.externalPower = true;
  TEST_ASSERT_NOT_EQUAL(calm, visual::countPixels(draw(rt, kQuiet + 720), visual::green));
  TEST_ASSERT_EQUAL(calm, visual::countPixels(draw(rt, kQuiet + 1200), visual::green));
}

void test_signal_pings_and_returns_to_full_brightness() {
  const auto rt = online("192.0.2.63", -50);
  TEST_ASSERT_LESS_THAN_INT(visual::countPixels(draw(rt, kQuiet), visual::brightNeutral),
                            visual::countPixels(draw(rt, 6400), visual::brightNeutral));
  TEST_ASSERT_TRUE(visual::sameFrame(draw(rt, kQuiet), draw(rt, 7000)));
}

void test_signal_changes_animate_then_show_the_new_strength() {
  Rig rig(online("192.0.2.63", -70));
  rig.until(kQuiet);
  for (int rssi : {-50, -85}) {
    const int64_t start = rig.last;
    rig.rt.wifiRssi = rssi;
    const Canvas moving = rig.until(start + 84);
    TEST_ASSERT_FALSE(visual::sameFrame(moving, draw(rig.rt, start + 84)));
    TEST_ASSERT_TRUE(visual::sameFrame(rig.until(start + 1000), draw(rig.rt, start + 1000)));
  }
}

void test_connecting_scans_and_address_changes_roll_in() {
  auto rt = online();
  rt.wifi.phase = net::LinkPhase::Connecting;
  rt.wifi.endpoint.clear();
  TEST_ASSERT_FALSE(visual::sameFrame(draw(rt, 280), draw(rt, 420)));
  Rig rig(online());
  rig.until(kQuiet);
  rig.rt.wifi.endpoint = "192.0.2.100";
  TEST_ASSERT_FALSE(visual::sameFrame(rig.until(kQuiet + 124), draw(rig.rt, kQuiet + 124)));
  TEST_ASSERT_TRUE(visual::sameShape(rig.until(kQuiet + 3000), draw(rig.rt, kQuiet + 3000)));
  TEST_ASSERT_TRUE(shows(rig.frame(kQuiet + 3000), "100"));
}

void test_losing_wifi_animates_then_shows_the_failure() {
  Rig rig(online());
  rig.until(kQuiet);
  rig.rt.wifi.phase = net::LinkPhase::Offline;
  rig.rt.wifi.endpoint.clear();
  rig.rt.wifiRssi = 0;
  TEST_ASSERT_FALSE(visual::sameFrame(rig.until(kQuiet + 124), draw(rig.rt, kQuiet + 124)));
  TEST_ASSERT_TRUE(shows(rig.until(kQuiet + 1500), "NO WIFI"));
  TEST_ASSERT_TRUE(visual::sameShape(rig.until(kQuiet + 3000), draw(rig.rt, kQuiet + 3000)));
}

void test_frame_gaps_skip_old_animations_but_short_stalls_keep_them() {
  for (int gap : {200, 1000}) {
    Rig rig(online());
    rig.until(kQuiet);
    rig.rt.batteryPercent = 40;
    TEST_ASSERT_EQUAL(gap == 1000,
                      visual::sameFrame(rig.frame(kQuiet + gap), draw(rig.rt, kQuiet + gap)));
  }
}

void test_the_same_inputs_produce_the_same_animation() {
  Rig a(online()), b(online());
  for (Rig* rig : {&a, &b}) {
    rig->until(kQuiet);
    rig->rt.batteryPercent = 20;
    rig->rt.externalPower = true;
    rig->rt.wifiRssi = -80;
  }
  for (int64_t now = kQuiet + 24; now < kQuiet + 1500; now += 24)
    TEST_ASSERT_TRUE(visual::sameFrame(a.until(now), b.until(now)));
}

}

int main(int, char**) {
  UNITY_BEGIN();
  RUN_TEST(test_low_battery_blinks_but_keeps_its_percentage_readable);
  RUN_TEST(test_unknown_battery_has_a_placeholder_and_no_charge_fill);
  RUN_TEST(test_signal_strength_grows_with_reception);
  RUN_TEST(test_a_wide_address_remains_readable);
  RUN_TEST(test_connecting_and_offline_replace_the_address);
  RUN_TEST(test_battery_changes_animate_and_settle_without_covering_the_address);
  RUN_TEST(test_power_indication_is_independent_of_battery_readings);
  RUN_TEST(test_power_changes_animate_and_settle_without_covering_the_address);
  RUN_TEST(test_charging_animates_the_battery);
  RUN_TEST(test_signal_pings_and_returns_to_full_brightness);
  RUN_TEST(test_signal_changes_animate_then_show_the_new_strength);
  RUN_TEST(test_connecting_scans_and_address_changes_roll_in);
  RUN_TEST(test_losing_wifi_animates_then_shows_the_failure);
  RUN_TEST(test_frame_gaps_skip_old_animations_but_short_stalls_keep_them);
  RUN_TEST(test_the_same_inputs_produce_the_same_animation);
  return UNITY_END();
}
