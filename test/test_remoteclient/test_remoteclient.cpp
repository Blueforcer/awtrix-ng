#include <unity.h>

#include <string>
#include <vector>

#include "core/apps/AppRegistry.h"
#include "core/remote/RemoteApps.h"
#include "core/remote/RemoteClient.h"
#include "core/remote/RemoteFrames.h"

using namespace awtrix;
using namespace awtrix::remote;

namespace {

const net::Endpoint kHost{0xC0A80105u, 8301};
const net::Endpoint kStranger{0xC0A80109u, 8301};
constexpr int64_t kNow = 1790000000;

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

void sign(const uint8_t* data, std::size_t length, uint8_t* tag) {
  for (std::size_t i = 0; i < wire::kTagBytes; ++i) tag[i] = static_cast<uint8_t>(length + i + data[0]);
}

void otherSign(const uint8_t* data, std::size_t length, uint8_t* tag) {
  for (std::size_t i = 0; i < wire::kTagBytes; ++i) tag[i] = static_cast<uint8_t>(length * 3 + i + data[1]);
}

uint32_t g_random = 0;
uint32_t nextRandom() { return g_random += 0x10001u; }

uint32_t g_askedTowards = 0;
uint32_t g_ownAddress = 0;
uint32_t ownAddress(uint32_t host) {
  g_askedTowards = host;
  return g_ownAddress;
}

struct Clock {
  Recorder out;
  AppRegistry registry;
  RemoteFrames frames;
  std::vector<bool> holds;
  RemoteApps apps{registry, frames, nullptr, nullptr, [this](bool hold) { holds.push_back(hold); }};
  RemoteClient client{out, frames, apps};
  uint8_t buf[wire::kMaxDatagram];
  uint16_t revision = 1;

  Clock() {
    g_random = 0x12340000u;
    g_askedTowards = 0;
    g_ownAddress = 0xC0A8B214u;
    frames.begin(32, 8);
    client.begin("a1b2c3", 32, 8, 8080, ownAddress, sign, nextRandom);
    client.setHost(kHost);
  }

  void hostSendsApps(std::initializer_list<const char*> names, int64_t nowMs,
                     const net::Endpoint& from = kHost, uint8_t flags = wire::app::kWantsShow) {
    wire::Apps a;
    a.revision = revision++;
    for (const char* n : names) {
      a.entries[a.count].name = n;
      a.entries[a.count].flags = flags;
      ++a.count;
    }
    client.receive(from, buf, wire::encodeApps(a, buf, sizeof(buf)), nowMs);
  }

  void hostSendsFrame(const char* app, uint32_t rgb, int64_t nowMs) {
    Canvas c(32, 8);
    c.clear(rgb);
    client.receive(kHost, buf, wire::encodeFrame(c, app, 1, 0, 8, buf, sizeof(buf)), nowMs);
  }

  wire::Packet offer(uint16_t port, uint32_t unixTime, const char* uid = "a1b2c3",
                     const wire::Signer& signer = sign) {
    wire::Offer o;
    o.name = "pi";
    o.port = port;
    o.uid = uid;
    o.unixTime = unixTime;
    wire::Packet p;
    TEST_ASSERT_TRUE(wire::decode(buf, wire::encodeOffer(o, buf, sizeof(buf), signer), p));
    return p;
  }

  wire::Packet lastHello() const { return out.of(wire::Type::Hello).back(); }
};

}

void setUp() {}
void tearDown() {}

static void test_hello_goes_out_at_once_and_every_two_seconds() {
  Clock c;
  c.client.tick("Time", "", 0);
  TEST_ASSERT_EQUAL(1, c.out.count(wire::Type::Hello));
  c.client.tick("Time", "", RemoteClient::kHelloEveryMs - 1);
  TEST_ASSERT_EQUAL(1, c.out.count(wire::Type::Hello));
  c.client.tick("Time", "", RemoteClient::kHelloEveryMs);
  const auto hellos = c.out.of(wire::Type::Hello);
  TEST_ASSERT_EQUAL(2, hellos.size());
  TEST_ASSERT_EQUAL_MESSAGE(hellos[0].hello.counter + 1, hellos[1].hello.counter, "the counter rises");
  TEST_ASSERT_EQUAL(32, hellos[0].hello.width);
  TEST_ASSERT_TRUE(c.out.sent[0].first == kHost);
}

static void test_hello_names_the_address_the_clock_has_towards_its_host() {
  Clock c;
  c.client.tick("", "", 0);
  TEST_ASSERT_EQUAL_HEX32(kHost.address, g_askedTowards);
  TEST_ASSERT_EQUAL_HEX32(0xC0A8B214u, c.lastHello().hello.address);
  TEST_ASSERT_EQUAL_UINT16(8080, c.lastHello().hello.webPort);
  g_ownAddress = 0xC0A8B215u;
  c.client.setHost(kStranger);
  c.client.tick("", "", 100);
  TEST_ASSERT_EQUAL_HEX32_MESSAGE(kStranger.address, g_askedTowards, "asked again for the new host");
  TEST_ASSERT_EQUAL_HEX32_MESSAGE(0xC0A8B215u, c.lastHello().hello.address, "an address that changed goes out");
  g_ownAddress = 0;
  c.client.tick("", "", 100 + RemoteClient::kHelloEveryMs);
  TEST_ASSERT_EQUAL_HEX32_MESSAGE(0, c.lastHello().hello.address, "no address of its own: none is named");
}

static void test_counter_and_revisions_start_where_chance_puts_them() {
  Clock c;
  c.client.tick("", "", 0);
  const wire::Hello first = c.lastHello().hello;
  TEST_ASSERT_EQUAL_HEX32(0x12350001u + 1, first.counter);
  TEST_ASSERT_EQUAL_HEX16(0x0002, first.settingsRevision);
  TEST_ASSERT_EQUAL_HEX16(0x0003, first.systemRevision);
  TEST_ASSERT_EQUAL_HEX16(0x0004, first.assetsRevision);
}

static void test_each_revision_moves_with_its_own_change() {
  Clock c;
  c.client.tick("", "", 0);
  const wire::Hello before = c.lastHello().hello;
  c.client.settingsChanged();
  c.client.tick("", "", 100);
  TEST_ASSERT_EQUAL_MESSAGE(1, c.out.count(wire::Type::Hello), "a revision waits for the next Hello");
  c.client.tick("", "", RemoteClient::kHelloEveryMs);
  wire::Hello after = c.lastHello().hello;
  TEST_ASSERT_EQUAL(before.settingsRevision + 1, after.settingsRevision);
  TEST_ASSERT_EQUAL(before.systemRevision, after.systemRevision);
  TEST_ASSERT_EQUAL(before.assetsRevision, after.assetsRevision);
  c.client.systemChanged();
  c.client.assetsChanged();
  c.client.assetsChanged();
  c.client.tick("", "", 2 * RemoteClient::kHelloEveryMs);
  after = c.lastHello().hello;
  TEST_ASSERT_EQUAL(before.systemRevision + 1, after.systemRevision);
  TEST_ASSERT_EQUAL(before.assetsRevision + 2, after.assetsRevision);
}

static void test_hello_carries_state_and_sensors() {
  Clock c;
  wire::Sensors s;
  s.present = wire::sensor::kTemperature;
  s.temperatureCenti = 2150;
  c.client.setState(wire::state::kDisplayOn, s);
  c.client.tick("", "", 0);
  const wire::Packet p = c.out.of(wire::Type::Hello)[0];
  TEST_ASSERT_EQUAL(wire::state::kDisplayOn, p.hello.state);
  TEST_ASSERT_EQUAL(2150, p.hello.sensors.temperatureCenti);
}

static void test_an_applied_list_and_a_switched_off_app_are_told_at_once() {
  Clock c;
  c.client.tick("Time", "", 0);
  c.revision = 40;
  c.hostSendsApps({"Weather", "Stocks", "Game"}, 10);
  c.client.tick("Time", "", 20);
  TEST_ASSERT_EQUAL(2, c.out.count(wire::Type::Hello));
  TEST_ASSERT_EQUAL(40, c.lastHello().hello.appsRevision);
  TEST_ASSERT_EQUAL_HEX32(0, c.lastHello().hello.offMask);
  c.apps.setRunningScripts({"Weather"});
  c.client.tick("Time", "", 30);
  TEST_ASSERT_EQUAL(3, c.out.count(wire::Type::Hello));
  TEST_ASSERT_EQUAL_HEX32_MESSAGE(0b110, c.lastHello().hello.offMask, "bit i is entry i of the list");
  c.client.tick("Time", "", 40);
  TEST_ASSERT_EQUAL(3, c.out.count(wire::Type::Hello));
}

static void test_show_names_only_host_apps_and_follows_changes() {
  Clock c;
  c.hostSendsApps({"Weather", "Stocks"}, 0);
  c.client.tick("Time", "Weather", 0);
  auto shows = c.out.of(wire::Type::Show);
  TEST_ASSERT_EQUAL(1, shows.size());
  TEST_ASSERT_EQUAL_MESSAGE(0, shows[0].show.current.size(), "a built-in app is not asked for");
  TEST_ASSERT_EQUAL_STRING_LEN("Weather", shows[0].show.incoming.data(), shows[0].show.incoming.size());
  c.client.tick("Time", "Weather", 100);
  TEST_ASSERT_EQUAL_MESSAGE(1, c.out.count(wire::Type::Show), "nothing changed, keepalive not due");
  c.client.tick("Time", "Weather", RemoteClient::kShowEveryMs);
  TEST_ASSERT_EQUAL(2, c.out.count(wire::Type::Show));
  c.client.tick("Weather", "", RemoteClient::kShowEveryMs + 1);
  shows = c.out.of(wire::Type::Show);
  TEST_ASSERT_EQUAL(3, shows.size());
  TEST_ASSERT_EQUAL_STRING_LEN("Weather", shows[2].show.current.data(), shows[2].show.current.size());
  TEST_ASSERT_EQUAL_MESSAGE(shows[0].show.sequence + 1, shows[2].show.sequence, "a change counts up");
}

static void test_no_show_keepalive_while_nothing_is_asked_for() {
  Clock c;
  c.hostSendsApps({"Weather"}, 0);
  c.client.tick("Time", "", 0);
  c.client.tick("Time", "", 1000);
  TEST_ASSERT_EQUAL(0, c.out.count(wire::Type::Show));
  c.client.tick("Weather", "", 1100);
  c.client.tick("Time", "", 1200);
  TEST_ASSERT_EQUAL_MESSAGE(2, c.out.count(wire::Type::Show), "the end of a request is still told");
  c.client.tick("Time", "", 2000);
  TEST_ASSERT_EQUAL(2, c.out.count(wire::Type::Show));
}

static void test_buttons_are_sent_at_once() {
  Clock c;
  c.hostSendsApps({"Game"}, 0);
  c.client.tick("Game", "", 0);
  const uint8_t presses[3] = {0, 1, 0};
  c.client.setButtons(0b010, presses);
  c.client.tick("Game", "", 10);
  const auto shows = c.out.of(wire::Type::Show);
  TEST_ASSERT_EQUAL(2, shows.size());
  TEST_ASSERT_EQUAL(0b010, shows[1].show.buttonsDown);
  TEST_ASSERT_EQUAL(1, shows[1].show.presses[1]);
}

static void test_the_link_is_up_while_the_host_talks() {
  Clock c;
  c.client.tick("Time", "", 0);
  TEST_ASSERT_FALSE(c.client.linkUp());
  c.hostSendsApps({"Weather"}, 100);
  TEST_ASSERT_TRUE(c.client.linkUp());
  TEST_ASSERT_TRUE(c.apps.scriptWantsShow("Weather"));
  c.client.tick("Time", "", 100 + RemoteClient::kSilenceMs);
  TEST_ASSERT_TRUE(c.client.linkUp());
  c.client.tick("Time", "", 101 + RemoteClient::kSilenceMs);
  TEST_ASSERT_FALSE(c.client.linkUp());
  TEST_ASSERT_FALSE_MESSAGE(c.apps.scriptWantsShow("Weather"), "the rotation skips host apps");
}

static void test_a_silent_host_lets_go_of_everything_it_held() {
  Clock c;
  c.hostSendsApps({"Game"}, 0, kHost,
                  wire::app::kWantsShow | wire::app::kTakesButtons | wire::app::kScrollHolds);
  c.apps.hold(true);
  TEST_ASSERT_TRUE(c.apps.takesButtons("Game"));
  TEST_ASSERT_TRUE(c.apps.scriptScrollHolds("Game"));
  c.client.tick("Game", "", 1 + RemoteClient::kSilenceMs);
  TEST_ASSERT_FALSE(c.apps.takesButtons("Game"));
  TEST_ASSERT_FALSE(c.apps.scriptScrollHolds("Game"));
  TEST_ASSERT_FALSE(c.apps.scriptWantsShow("Game"));
  TEST_ASSERT_EQUAL(2, c.holds.size());
  TEST_ASSERT_FALSE_MESSAGE(c.holds.back(), "the hold the host set ends");
}

static void test_a_dropped_link_is_down_at_once() {
  Clock c;
  c.hostSendsApps({"Game"}, 0);
  c.apps.hold(true);
  c.client.dropLink();
  TEST_ASSERT_FALSE(c.client.linkUp());
  TEST_ASSERT_FALSE(c.apps.scriptWantsShow("Game"));
  TEST_ASSERT_FALSE(c.holds.back());
}

static void test_strangers_are_ignored() {
  Clock c;
  c.hostSendsApps({"Weather"}, 0, kStranger);
  TEST_ASSERT_FALSE(c.client.linkUp());
  TEST_ASSERT_FALSE(c.apps.owns("Weather"));
}

static void test_frames_reach_the_app_on_the_display() {
  Clock c;
  c.hostSendsApps({"Weather"}, 0);
  c.client.tick("Weather", "", 0);
  c.hostSendsFrame("Weather", 0x00FF00, 10);
  Canvas out(32, 8);
  TEST_ASSERT_TRUE(c.frames.draw("Weather", out));
  TEST_ASSERT_EQUAL_HEX32(0x00FF00, out.getPixel(0, 0));
}

static void test_a_new_host_starts_over() {
  Clock c;
  c.hostSendsApps({"Weather"}, 0);
  c.client.tick("Weather", "", 0);
  const int hellos = c.out.count(wire::Type::Hello);
  c.client.setHost(kStranger);
  TEST_ASSERT_FALSE(c.client.linkUp());
  c.client.tick("Weather", "", 10);
  TEST_ASSERT_EQUAL_MESSAGE(hellos + 1, c.out.count(wire::Type::Hello), "the new host is greeted at once");
  TEST_ASSERT_TRUE(c.out.sent.back().first == kStranger || c.out.sent[c.out.sent.size() - 2].first == kStranger);
}

static void test_a_signed_offer_moves_a_link_that_is_down_to_its_sender() {
  Clock c;
  c.hostSendsApps({"Weather"}, 0);
  c.client.dropLink();
  TEST_ASSERT_TRUE(c.client.follow(c.offer(8302, kNow), kStranger.address, kNow + 5));
  TEST_ASSERT_FALSE_MESSAGE(c.client.linkUp(), "the new address has to answer first");
  c.client.tick("Time", "", 10);
  TEST_ASSERT_TRUE(c.out.sent.back().first == (net::Endpoint{kStranger.address, 8302}));
}

static void test_a_link_that_is_up_stays_where_it_is() {
  Clock c;
  c.hostSendsApps({"Weather"}, 0);
  TEST_ASSERT_FALSE_MESSAGE(c.client.follow(c.offer(8302, kNow), kStranger.address, kNow), "a host on two addresses");
  TEST_ASSERT_TRUE(c.client.linkUp());
  c.client.tick("Time", "", 10);
  TEST_ASSERT_TRUE(c.out.sent.back().first == kHost);
  c.client.tick("Time", "", 1 + RemoteClient::kSilenceMs);
  TEST_ASSERT_FALSE(c.client.linkUp());
  TEST_ASSERT_TRUE_MESSAGE(c.client.follow(c.offer(8302, kNow), kStranger.address, kNow),
                           "the same offer moves the link once it is down");
}

static void test_offers_that_must_not_move_the_link() {
  Clock c;
  TEST_ASSERT_FALSE_MESSAGE(c.client.follow(c.offer(8302, kNow, "other"), kStranger.address, kNow), "another clock's");
  TEST_ASSERT_FALSE_MESSAGE(c.client.follow(c.offer(8302, kNow, "a1b2c3", otherSign), kStranger.address, kNow),
                            "another key's");
  TEST_ASSERT_FALSE_MESSAGE(c.client.follow(c.offer(8302, kNow - 121), kStranger.address, kNow), "too old");
  TEST_ASSERT_FALSE_MESSAGE(c.client.follow(c.offer(8302, kNow + 121), kStranger.address, kNow), "from the future");
  TEST_ASSERT_FALSE_MESSAGE(c.client.follow(c.offer(8302, 1000), kStranger.address, 1000), "the clock has no time");
  TEST_ASSERT_FALSE_MESSAGE(c.client.follow(c.offer(kHost.port, kNow), kHost.address, kNow), "the same address");
  TEST_ASSERT_FALSE_MESSAGE(c.client.follow(c.offer(8302, kNow), kStranger.address, kNow),
                            "an offer already taken, even from elsewhere");
  wire::Offer plain;
  plain.name = "pi";
  plain.port = 8302;
  wire::Packet p;
  TEST_ASSERT_TRUE(wire::decode(c.buf, wire::encodeOffer(plain, c.buf, sizeof(c.buf)), p));
  TEST_ASSERT_FALSE_MESSAGE(c.client.follow(p, kStranger.address, kNow + 1), "a plain offer");
  c.client.tick("Time", "", 0);
  TEST_ASSERT_TRUE(c.out.sent.back().first == kHost);
}

static void test_nothing_is_sent_without_a_host() {
  Recorder out;
  AppRegistry registry;
  RemoteFrames frames;
  RemoteApps apps{registry, frames, nullptr, nullptr, nullptr};
  RemoteClient client{out, frames, apps};
  frames.begin(32, 8);
  client.begin("a1b2c3", 32, 8, 80, ownAddress, sign, nextRandom);
  client.tick("Time", "", 0);
  TEST_ASSERT_EQUAL(0, out.sent.size());
}

int main(int, char**) {
  UNITY_BEGIN();
  RUN_TEST(test_hello_goes_out_at_once_and_every_two_seconds);
  RUN_TEST(test_hello_names_the_address_the_clock_has_towards_its_host);
  RUN_TEST(test_counter_and_revisions_start_where_chance_puts_them);
  RUN_TEST(test_each_revision_moves_with_its_own_change);
  RUN_TEST(test_hello_carries_state_and_sensors);
  RUN_TEST(test_an_applied_list_and_a_switched_off_app_are_told_at_once);
  RUN_TEST(test_show_names_only_host_apps_and_follows_changes);
  RUN_TEST(test_no_show_keepalive_while_nothing_is_asked_for);
  RUN_TEST(test_buttons_are_sent_at_once);
  RUN_TEST(test_the_link_is_up_while_the_host_talks);
  RUN_TEST(test_a_silent_host_lets_go_of_everything_it_held);
  RUN_TEST(test_a_dropped_link_is_down_at_once);
  RUN_TEST(test_strangers_are_ignored);
  RUN_TEST(test_frames_reach_the_app_on_the_display);
  RUN_TEST(test_a_new_host_starts_over);
  RUN_TEST(test_a_signed_offer_moves_a_link_that_is_down_to_its_sender);
  RUN_TEST(test_a_link_that_is_up_stays_where_it_is);
  RUN_TEST(test_offers_that_must_not_move_the_link);
  RUN_TEST(test_nothing_is_sent_without_a_host);
  return UNITY_END();
}
