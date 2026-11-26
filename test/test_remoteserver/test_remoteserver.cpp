#include <unity.h>

#include <string>
#include <vector>

#include "core/remote/RemoteServer.h"

using namespace awtrix;
using namespace awtrix::remote;

namespace {

const net::Endpoint kClock{0xC0A80127u, 50000};
const net::Endpoint kStranger{0xC0A80199u, 50000};
const net::Endpoint kClockWeb{0xC0A8B214u, 8080};
constexpr uint16_t kSeed = 100;

struct Recorder : net::IDatagramSink {
  std::vector<std::pair<net::Endpoint, std::vector<uint8_t>>> sent;
  bool send(const net::Endpoint& to, const uint8_t* data, std::size_t length) override {
    sent.push_back({to, std::vector<uint8_t>(data, data + length)});
    return true;
  }
  std::vector<wire::Packet> of(wire::Type type) const {
    std::vector<wire::Packet> out;
    for (const auto& s : sent) {
      wire::Packet p;
      TEST_ASSERT_TRUE(wire::decode(s.second.data(), s.second.size(), p));
      if (p.type == type) out.push_back(p);
    }
    return out;
  }
  int count(wire::Type type) const { return static_cast<int>(of(type).size()); }
};

void sign(const uint8_t*, std::size_t, uint8_t* tag) {
  for (std::size_t i = 0; i < wire::kTagBytes; ++i) tag[i] = 0xAB;
}

struct Host {
  Recorder out;
  bool accept = true;
  RemoteServer server{out, "a1b2c3", [this](const wire::Packet&) { return accept; }, kSeed};
  uint8_t buf[wire::kMaxDatagram];
  uint16_t sequence = 0;
  uint8_t presses[3] = {0, 0, 0};
  uint32_t counter = 7;
  uint16_t appsRevision = 0;
  uint32_t offMask = 0;
  net::Endpoint web = kClockWeb;

  std::size_t helloBytes(const char* uid = "a1b2c3") {
    wire::Hello h;
    h.uid = uid;
    h.width = 32;
    h.height = 8;
    h.address = web.address;
    h.webPort = web.port;
    h.counter = ++counter;
    h.settingsRevision = 11;
    h.systemRevision = 22;
    h.assetsRevision = 33;
    h.appsRevision = appsRevision;
    h.offMask = offMask;
    h.sensors.present = wire::sensor::kTemperature;
    h.sensors.temperatureCenti = 2125;
    return wire::encodeHello(h, sign, buf, sizeof(buf));
  }
  void hello(int64_t now, const char* uid = "a1b2c3", const net::Endpoint& from = kClock) {
    server.receive(from, buf, helloBytes(uid), now);
  }
  void show(const char* current, const char* incoming, uint8_t down, int64_t now, bool bump = true) {
    wire::Show s;
    if (bump) ++sequence;
    s.sequence = sequence;
    s.buttonsDown = down;
    for (int i = 0; i < 3; ++i) s.presses[i] = presses[i];
    s.current = current;
    s.incoming = incoming;
    server.receive(kClock, buf, wire::encodeShow(s, buf, sizeof(buf)), now);
  }
  bool step(int button) {
    bool pressed[3];
    server.stepButtons(pressed);
    return pressed[button];
  }
};

}

void setUp() {}
void tearDown() {}

static void test_a_valid_hello_connects_and_gets_the_app_list() {
  Host h;
  h.server.setApps({{"Weather", wire::app::kWantsShow, 0}}, 0);
  h.hello(10);
  TEST_ASSERT_TRUE(h.server.connected());
  TEST_ASSERT_TRUE(h.server.endpoint() == kClock);
  TEST_ASSERT_EQUAL(1u, h.server.connection());
  TEST_ASSERT_EQUAL(32, h.server.clock().width);
  TEST_ASSERT_EQUAL_HEX32(kClockWeb.address, h.server.clock().address);
  TEST_ASSERT_EQUAL_UINT16(kClockWeb.port, h.server.clock().webPort);
  TEST_ASSERT_EQUAL(2125, h.server.clock().sensors.temperatureCenti);
  TEST_ASSERT_EQUAL(11, h.server.clock().settingsRevision);
  TEST_ASSERT_EQUAL(22, h.server.clock().systemRevision);
  TEST_ASSERT_EQUAL(33, h.server.clock().assetsRevision);
  const auto apps = h.out.of(wire::Type::Apps);
  TEST_ASSERT_EQUAL(1, apps.size());
  TEST_ASSERT_EQUAL_MESSAGE(kSeed + 1, apps[0].apps.revision, "the revision counts on from the seed");
  TEST_ASSERT_EQUAL_STRING_LEN("Weather", apps[0].apps.entries[0].name.data(), 7);
}

static void test_no_app_list_goes_out_before_the_host_has_one() {
  Host h;
  h.hello(0);
  h.server.tick(RemoteServer::kAppsEveryMs + 1);
  TEST_ASSERT_EQUAL_MESSAGE(0, h.out.count(wire::Type::Apps), "an empty list would delete the host apps");
  h.server.setApps({}, 3000);
  TEST_ASSERT_EQUAL_MESSAGE(1, h.out.count(wire::Type::Apps), "an empty list the host means goes out");
  TEST_ASSERT_EQUAL(0, h.out.of(wire::Type::Apps)[0].apps.count);
}

static void test_a_wrong_tag_or_uid_is_ignored() {
  Host h;
  h.accept = false;
  h.hello(0);
  TEST_ASSERT_FALSE(h.server.connected());
  h.accept = true;
  h.hello(0, "other");
  TEST_ASSERT_FALSE(h.server.connected());
  TEST_ASSERT_EQUAL(0, h.out.sent.size());
}

static void test_a_replayed_hello_is_ignored_while_connected() {
  Host h;
  h.hello(0);
  const std::vector<uint8_t> captured(h.buf, h.buf + h.helloBytes());
  h.server.receive(kClock, captured.data(), captured.size(), 100);
  h.server.receive(kStranger, captured.data(), captured.size(), 200);
  TEST_ASSERT_TRUE_MESSAGE(h.server.endpoint() == kClock, "a replay from elsewhere does not move the clock");
  TEST_ASSERT_EQUAL(1u, h.server.connection());
  h.counter -= 5;
  h.hello(300, "a1b2c3", kStranger);
  TEST_ASSERT_TRUE_MESSAGE(h.server.endpoint() == kClock, "an older counter neither");
  h.counter += 10;
  h.hello(400, "a1b2c3", kStranger);
  TEST_ASSERT_TRUE_MESSAGE(h.server.endpoint() == kStranger, "a newer counter is the clock that moved");
  TEST_ASSERT_EQUAL(2u, h.server.connection());
}

static void test_the_web_address_is_what_the_hello_names_not_where_it_came_from() {
  Host h;
  h.hello(0, "a1b2c3", kStranger);
  TEST_ASSERT_TRUE(h.server.endpoint() == kStranger);
  TEST_ASSERT_EQUAL_HEX32_MESSAGE(kClockWeb.address, h.server.clock().address, "a relay's address is not the clock's");
  TEST_ASSERT_EQUAL_UINT16(kClockWeb.port, h.server.clock().webPort);
  h.web = {0xC0A8B215u, 80};
  h.hello(100, "a1b2c3", kStranger);
  TEST_ASSERT_EQUAL_HEX32_MESSAGE(0xC0A8B215u, h.server.clock().address, "a clock that moved names its new address");
  TEST_ASSERT_EQUAL_UINT16(80, h.server.clock().webPort);
  TEST_ASSERT_EQUAL(1u, h.server.connection());
  h.web = {};
  h.hello(200, "a1b2c3", kStranger);
  TEST_ASSERT_EQUAL_HEX32_MESSAGE(0, h.server.clock().address, "a clock that knows no address of its own says so");
  TEST_ASSERT_EQUAL_UINT16(0, h.server.clock().webPort);
  h.accept = false;
  h.web = kClockWeb;
  h.hello(300, "a1b2c3", kStranger);
  TEST_ASSERT_EQUAL_HEX32_MESSAGE(0, h.server.clock().address, "only a verified Hello moves the address");
}

static void test_a_clock_that_was_silent_starts_a_new_connection() {
  Host h;
  h.hello(0);
  h.hello(100);
  TEST_ASSERT_EQUAL(1u, h.server.connection());
  h.server.tick(100 + RemoteServer::kClockSilenceMs + 1);
  TEST_ASSERT_FALSE(h.server.connected());
  h.counter = 3;
  h.hello(7000);
  TEST_ASSERT_TRUE_MESSAGE(h.server.connected(), "a restarted clock counts from anywhere");
  TEST_ASSERT_EQUAL(2u, h.server.connection());
}

static void test_switched_off_apps_follow_the_mask_for_the_current_list() {
  Host h;
  h.server.setApps({{"A", 0, 0}, {"B", 0, 0}, {"C", 0, 0}}, 0);
  h.hello(0);
  TEST_ASSERT_EQUAL(0, h.server.switchedOff().size());
  h.appsRevision = kSeed + 1;
  h.offMask = 0b101;
  h.hello(100);
  TEST_ASSERT_EQUAL(2, h.server.switchedOff().size());
  TEST_ASSERT_EQUAL_STRING("A", h.server.switchedOff()[0].c_str());
  TEST_ASSERT_EQUAL_STRING("C", h.server.switchedOff()[1].c_str());
  h.server.setApps({{"C", 0, 0}, {"A", 0, 0}}, 200);
  h.offMask = 0;
  h.hello(300);
  TEST_ASSERT_EQUAL_MESSAGE(2, h.server.switchedOff().size(), "a mask for an older list is not read");
  h.appsRevision = kSeed + 2;
  h.offMask = 0b10;
  h.hello(400);
  TEST_ASSERT_EQUAL(1, h.server.switchedOff().size());
  TEST_ASSERT_EQUAL_STRING("A", h.server.switchedOff()[0].c_str());
}

static void test_switches_are_known_once_the_clock_answered_the_current_list() {
  Host h;
  h.server.setApps({{"A", 0, 0}, {"B", 0, 0}}, 0);
  h.appsRevision = 1234;
  h.hello(0);
  TEST_ASSERT_FALSE_MESSAGE(h.server.switchesKnown(), "a clock that knew an earlier host says nothing yet");
  TEST_ASSERT_EQUAL(0, h.server.switchedOff().size());
  h.appsRevision = kSeed + 1;
  h.hello(100);
  TEST_ASSERT_TRUE_MESSAGE(h.server.switchesKnown(), "nothing switched off is known now");
  TEST_ASSERT_EQUAL(0, h.server.switchedOff().size());
  TEST_ASSERT_EQUAL(h.counter, h.server.clock().counter);
  h.server.setApps({{"A", 0, 0}, {"B", 0, 0}, {"C", 0, 0}}, 200);
  TEST_ASSERT_FALSE_MESSAGE(h.server.switchesKnown(), "a new list waits for the clock again");
  h.hello(300);
  TEST_ASSERT_FALSE(h.server.switchesKnown());
  h.appsRevision = kSeed + 2;
  h.offMask = 0b100;
  h.hello(400);
  TEST_ASSERT_TRUE(h.server.switchesKnown());
  TEST_ASSERT_EQUAL_STRING("C", h.server.switchedOff()[0].c_str());
  h.server.tick(400 + RemoteServer::kClockSilenceMs + 1);
  h.appsRevision = 77;
  h.hello(8000);
  TEST_ASSERT_FALSE_MESSAGE(h.server.switchesKnown(), "a new connection waits for the clock again");
  h.appsRevision = kSeed + 2;
  h.hello(8100);
  TEST_ASSERT_TRUE(h.server.switchesKnown());
}

static void test_names_that_cannot_travel_leave_the_list_before_it_is_counted() {
  Host h;
  h.server.setApps({{"", 0, 0}, {"A", 0, 0}, {std::string(wire::kMaxName + 1, 'x'), 0, 0}, {"B", 0, 0}}, 0);
  h.hello(0);
  const auto apps = h.out.of(wire::Type::Apps);
  TEST_ASSERT_EQUAL(2, apps[0].apps.count);
  h.appsRevision = kSeed + 1;
  h.offMask = 0b10;
  h.hello(100);
  TEST_ASSERT_EQUAL(1, h.server.switchedOff().size());
  TEST_ASSERT_EQUAL_STRING("B", h.server.switchedOff()[0].c_str());
}

static void test_the_app_list_follows_changes_and_repeats() {
  Host h;
  h.hello(0);
  h.server.setApps({{"A", 0, 0}}, 100);
  h.server.setApps({{"A", 0, 0}}, 200);
  TEST_ASSERT_EQUAL_MESSAGE(1, h.out.count(wire::Type::Apps), "an unchanged list is not resent at once");
  h.server.setApps({{"A", wire::app::kTakesButtons, 0}}, 300);
  TEST_ASSERT_EQUAL(2, h.out.count(wire::Type::Apps));
  h.server.tick(300 + RemoteServer::kAppsEveryMs);
  TEST_ASSERT_EQUAL(3, h.out.count(wire::Type::Apps));
}

static void test_show_names_the_apps_to_draw_until_it_stops() {
  Host h;
  h.hello(0);
  h.show("Weather", "Stocks", 0, 10);
  TEST_ASSERT_EQUAL_STRING("Weather", h.server.current().c_str());
  TEST_ASSERT_EQUAL_STRING("Stocks", h.server.incoming().c_str());
  h.server.tick(10 + RemoteServer::kShowSilenceMs + 1);
  TEST_ASSERT_TRUE(h.server.current().empty());
}

static void test_an_older_show_is_dropped() {
  Host h;
  h.hello(0);
  h.show("Weather", "", 0, 10);
  h.sequence -= 2;
  h.show("Old", "", 0, 20);
  TEST_ASSERT_EQUAL_STRING("Weather", h.server.current().c_str());
}

static void test_after_a_silence_any_show_starts_over() {
  Host h;
  h.hello(0);
  h.sequence = 500;
  h.show("Weather", "", 0, 10);
  h.server.tick(10 + RemoteServer::kShowSilenceMs + 1);
  h.sequence = 0;
  h.presses[0] = 9;
  h.show("Game", "", 0, 2000);
  TEST_ASSERT_EQUAL_STRING_MESSAGE("Game", h.server.current().c_str(), "a restarted clock counts from one");
  TEST_ASSERT_FALSE_MESSAGE(h.step(0), "its press count is where it starts, not presses");
}

static void test_show_from_a_stranger_is_ignored() {
  Host h;
  h.hello(0);
  wire::Show s;
  s.sequence = 1;
  s.current = "Weather";
  h.server.receive(kStranger, h.buf, wire::encodeShow(s, h.buf, sizeof(h.buf)), 10);
  TEST_ASSERT_TRUE(h.server.current().empty());
}

static void test_a_short_tap_is_one_frame_of_press() {
  Host h;
  h.hello(0);
  h.show("Game", "", 0, 10);
  h.presses[2] = 1;
  h.show("Game", "", 0, 20);
  TEST_ASSERT_TRUE_MESSAGE(h.step(2), "a tap the down bit never showed still presses");
  TEST_ASSERT_FALSE(h.step(2));
  TEST_ASSERT_FALSE(h.step(0));
}

static void test_a_hold_lasts_until_release() {
  Host h;
  h.hello(0);
  h.show("Game", "", 0, 10);
  h.presses[0] = 1;
  h.show("Game", "", 0b001, 20);
  TEST_ASSERT_TRUE(h.step(0));
  TEST_ASSERT_TRUE(h.step(0));
  h.show("Game", "", 0, 30);
  TEST_ASSERT_FALSE(h.step(0));
}

static void test_two_quick_taps_are_two_presses() {
  Host h;
  h.hello(0);
  h.show("Game", "", 0, 10);
  h.presses[1] = 2;
  h.show("Game", "", 0, 20);
  TEST_ASSERT_TRUE(h.step(1));
  TEST_ASSERT_FALSE_MESSAGE(h.step(1), "a frame up between the two");
  TEST_ASSERT_TRUE(h.step(1));
  TEST_ASSERT_FALSE(h.step(1));
}

static void test_the_press_counter_wraps() {
  Host h;
  h.hello(0);
  h.presses[0] = 255;
  h.show("Game", "", 0, 10);
  h.presses[0] = 0;
  h.show("Game", "", 0, 20);
  TEST_ASSERT_TRUE(h.step(0));
  TEST_ASSERT_FALSE(h.step(0));
  TEST_ASSERT_FALSE_MESSAGE(h.step(0), "one press, not 256");
}

static void test_frames_go_out_on_change_and_as_keepalive() {
  Host h;
  h.hello(0);
  h.show("Weather", "", 0, 0);
  Canvas c(32, 8);
  c.clear(0x00FF00);
  h.server.sendFrame("Weather", c, 100);
  h.server.sendFrame("Weather", c, 200);
  TEST_ASSERT_EQUAL(1, h.out.count(wire::Type::Frame));
  c.setPixel(0, 0, 0xFF0000);
  h.server.sendFrame("Weather", c, 300);
  TEST_ASSERT_EQUAL(2, h.out.count(wire::Type::Frame));
  h.server.sendFrame("Weather", c, 300 + RemoteServer::kKeepaliveMs);
  TEST_ASSERT_EQUAL(3, h.out.count(wire::Type::Frame));
}

static void test_a_wide_frame_is_split() {
  Host h;
  wire::Hello hello;
  hello.uid = "a1b2c3";
  hello.width = 128;
  hello.height = 8;
  h.server.receive(kClock, h.buf, wire::encodeHello(hello, sign, h.buf, sizeof(h.buf)), 0);
  Canvas c(128, 8);
  h.server.sendFrame("Game", c, 10);
  const auto frames = h.out.of(wire::Type::Frame);
  TEST_ASSERT_GREATER_THAN(1, frames.size());
  int rows = 0;
  for (const auto& f : frames) rows += f.frame.rows;
  TEST_ASSERT_EQUAL(8, rows);
}

static void test_a_silent_clock_is_forgotten() {
  Host h;
  h.hello(0);
  h.show("Weather", "", 0, 0);
  h.server.tick(RemoteServer::kClockSilenceMs);
  TEST_ASSERT_TRUE(h.server.connected());
  h.server.tick(RemoteServer::kClockSilenceMs + 1);
  TEST_ASSERT_FALSE(h.server.connected());
  TEST_ASSERT_TRUE(h.server.current().empty());
  Canvas c(32, 8);
  h.server.sendFrame("Weather", c, 7000);
  TEST_ASSERT_EQUAL(0, h.out.count(wire::Type::Frame));
}

static void test_a_clock_that_moved_is_followed() {
  Host h;
  h.hello(0);
  h.hello(100, "a1b2c3", kStranger);
  TEST_ASSERT_TRUE(h.server.endpoint() == kStranger);
}

int main(int, char**) {
  UNITY_BEGIN();
  RUN_TEST(test_a_valid_hello_connects_and_gets_the_app_list);
  RUN_TEST(test_no_app_list_goes_out_before_the_host_has_one);
  RUN_TEST(test_a_wrong_tag_or_uid_is_ignored);
  RUN_TEST(test_a_replayed_hello_is_ignored_while_connected);
  RUN_TEST(test_the_web_address_is_what_the_hello_names_not_where_it_came_from);
  RUN_TEST(test_a_clock_that_was_silent_starts_a_new_connection);
  RUN_TEST(test_switched_off_apps_follow_the_mask_for_the_current_list);
  RUN_TEST(test_switches_are_known_once_the_clock_answered_the_current_list);
  RUN_TEST(test_names_that_cannot_travel_leave_the_list_before_it_is_counted);
  RUN_TEST(test_the_app_list_follows_changes_and_repeats);
  RUN_TEST(test_show_names_the_apps_to_draw_until_it_stops);
  RUN_TEST(test_an_older_show_is_dropped);
  RUN_TEST(test_after_a_silence_any_show_starts_over);
  RUN_TEST(test_show_from_a_stranger_is_ignored);
  RUN_TEST(test_a_short_tap_is_one_frame_of_press);
  RUN_TEST(test_a_hold_lasts_until_release);
  RUN_TEST(test_two_quick_taps_are_two_presses);
  RUN_TEST(test_the_press_counter_wraps);
  RUN_TEST(test_frames_go_out_on_change_and_as_keepalive);
  RUN_TEST(test_a_wide_frame_is_split);
  RUN_TEST(test_a_silent_clock_is_forgotten);
  RUN_TEST(test_a_clock_that_moved_is_followed);
  return UNITY_END();
}
