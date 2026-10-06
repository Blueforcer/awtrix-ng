#include <unity.h>

#include <cstring>
#include <string>
#include <vector>

#include "core/mirror/Mirror.h"
#include "core/mirror/MirrorFilter.h"
#include "core/mirror/MirrorSource.h"
#include "core/mirror/MirrorWire.h"

using namespace awtrix;
using namespace awtrix::mirror;

namespace {

struct Sent {
  net::Endpoint to;
  std::vector<uint8_t> bytes;
};

struct Recorder : net::IDatagramSink {
  std::vector<Sent> sent;
  bool send(const net::Endpoint& to, const uint8_t* data, std::size_t length) override {
    sent.push_back({to, std::vector<uint8_t>(data, data + length)});
    return true;
  }
  std::vector<wire::Packet> packets() const {
    std::vector<wire::Packet> out;
    for (const Sent& s : sent) {
      wire::Packet p;
      TEST_ASSERT_TRUE(wire::decode(s.bytes.data(), s.bytes.size(), p));
      out.push_back(p);
    }
    return out;
  }
  int count(wire::Type type) const {
    int n = 0;
    for (const Sent& s : sent) n += s.bytes.size() > 5 && s.bytes[5] == static_cast<uint8_t>(type);
    return n;
  }
};

PageInfo app(const std::string& name, const std::string& incoming = "") {
  PageInfo page;
  page.kind = PageKind::App;
  page.app = name;
  page.incoming = incoming;
  return page;
}

PageInfo notification() {
  PageInfo page;
  page.kind = PageKind::Notification;
  return page;
}

void paint(Canvas& canvas, uint32_t seed) {
  for (int y = 0; y < canvas.height(); ++y)
    for (int x = 0; x < canvas.width(); ++x)
      canvas.setPixel(x, y, (seed * 2654435761u + static_cast<uint32_t>(y * 131 + x)) & 0xFFFFFFu);
}

bool same(const Canvas& a, const Canvas& b) {
  if (a.width() != b.width() || a.height() != b.height()) return false;
  for (int y = 0; y < a.height(); ++y)
    for (int x = 0; x < a.width(); ++x)
      if (a.getPixel(x, y) != b.getPixel(x, y)) return false;
  return true;
}

// One clock on the fake network: its own panel, what it shows and the datagrams it sent.
struct Node {
  net::Endpoint self;
  Recorder out;
  Mirror mirror{out};
  Status status;
  Canvas panel;
  PageInfo page = app("Time");
  bool hasPage = true;

  Node(uint32_t address, int width = 32, int height = 8)
      : self{address, wire::kPort}, panel(width, height) {
    mirror.begin(width, height, status);
    mirror.setOnline(true);
  }
};

// Delivers what from sent to to; drop decides per datagram whether the network loses it.
template <typename Drop>
void deliver(Node& from, Node& to, int64_t nowMs, Drop drop) {
  std::vector<Sent> pending;
  pending.swap(from.out.sent);
  for (std::size_t i = 0; i < pending.size(); ++i) {
    if (pending[i].to != to.self) {
      from.out.sent.push_back(pending[i]);
      continue;
    }
    if (drop(i, pending[i])) continue;
    to.mirror.receive(from.self, pending[i].bytes.data(), pending[i].bytes.size(), nowMs);
  }
}

void deliver(Node& from, Node& to, int64_t nowMs) {
  deliver(from, to, nowMs, [](std::size_t, const Sent&) { return false; });
}

// One frame on both clocks: timers, then what the viewer asks for, then what the shared clock shows.
void frame(Node& shared, Node& viewer, int64_t nowMs) {
  shared.mirror.tick(nowMs);
  viewer.mirror.tick(nowMs);
  deliver(viewer, shared, nowMs);
  shared.mirror.content(shared.panel, shared.hasPage ? &shared.page : nullptr, nowMs);
  deliver(shared, viewer, nowMs);
}

Config sharing(const std::string& apps = "*", bool notifications = true) {
  Config config;
  config.share = true;
  config.shareApps = apps;
  config.shareNotifications = notifications;
  return config;
}

Config following(const std::string& host, const std::string& apps = "*",
                 bool notifications = true) {
  Config config;
  config.follow = host;
  config.followApps = apps;
  config.followNotifications = notifications;
  return config;
}

// A shares, B follows A and is told where A is.
struct Pair {
  Node a;
  Node b;
  explicit Pair(int widthB = 32, int heightB = 8, Config viewerConfig = following("a.local"))
      : a(0x0A000001u), b(0x0A000002u, widthB, heightB) {
    a.mirror.configure(sharing());
    b.mirror.configure(viewerConfig);
    b.mirror.setSource(a.self);
    paint(a.panel, 1);
  }
};

}

void setUp() {}
void tearDown() {}

static void test_a_frame_survives_the_wire() {
  Canvas canvas(32, 8);
  paint(canvas, 7);
  wire::FrameHeader header;
  header.frame = 513;
  header.kind = PageKind::Notification;
  header.app = "Weather";
  header.incoming = "Time";
  uint8_t buffer[wire::kMaxDatagram];
  const std::size_t length = wire::encodeFrame(canvas, header, 2, 3, buffer, sizeof(buffer));
  TEST_ASSERT_EQUAL(19u + 7u + 4u + 32u * 3u * 3u, length);

  wire::Packet packet;
  TEST_ASSERT_TRUE(wire::decode(buffer, length, packet));
  TEST_ASSERT_EQUAL(static_cast<int>(wire::Type::Frame), static_cast<int>(packet.type));
  TEST_ASSERT_EQUAL(32, packet.width);
  TEST_ASSERT_EQUAL(8, packet.height);
  TEST_ASSERT_EQUAL(513, packet.frame);
  TEST_ASSERT_EQUAL(2, packet.firstRow);
  TEST_ASSERT_EQUAL(3, packet.rows);
  TEST_ASSERT_EQUAL(static_cast<int>(PageKind::Notification), static_cast<int>(packet.kind));
  TEST_ASSERT_EQUAL_STRING("Weather", std::string(packet.app).c_str());
  TEST_ASSERT_EQUAL_STRING("Time", std::string(packet.incoming).c_str());
  const uint32_t pixel = canvas.getPixel(5, 3);
  const uint8_t* rgb = packet.rgb + (1 * 32 + 5) * 3;
  TEST_ASSERT_EQUAL_HEX8(pixel >> 16, rgb[0]);
  TEST_ASSERT_EQUAL_HEX8((pixel >> 8) & 0xFF, rgb[1]);
  TEST_ASSERT_EQUAL_HEX8(pixel & 0xFF, rgb[2]);
}

static void test_malformed_datagrams_are_refused() {
  Canvas canvas(32, 8);
  wire::FrameHeader header;
  uint8_t good[wire::kMaxDatagram];
  const std::size_t length = wire::encodeFrame(canvas, header, 0, 8, good, sizeof(good));
  wire::Packet packet;
  TEST_ASSERT_TRUE(wire::decode(good, length, packet));

  std::vector<uint8_t> bad(good, good + length);
  bad[0] = 'X';
  TEST_ASSERT_FALSE(wire::decode(bad.data(), bad.size(), packet));
  bad.assign(good, good + length);
  bad[4] = 2;
  TEST_ASSERT_FALSE_MESSAGE(wire::decode(bad.data(), bad.size(), packet), "another version");
  TEST_ASSERT_FALSE_MESSAGE(wire::decode(good, length - 1, packet), "a pixel byte missing");
  bad.assign(good, good + length);
  bad.push_back(0);
  TEST_ASSERT_FALSE_MESSAGE(wire::decode(bad.data(), bad.size(), packet), "a byte too many");
  bad.assign(good, good + length);
  bad[12] = 1;
  TEST_ASSERT_FALSE_MESSAGE(wire::decode(bad.data(), bad.size(), packet), "rows past the end");
  bad.assign(good, good + length);
  bad[16] = 2;
  TEST_ASSERT_FALSE_MESSAGE(wire::decode(bad.data(), bad.size(), packet), "an unknown page kind");
  bad.assign(good, good + length);
  bad[17] = 200;
  TEST_ASSERT_FALSE_MESSAGE(wire::decode(bad.data(), bad.size(), packet), "a name past the end");

  uint8_t idle[16];
  TEST_ASSERT_EQUAL(10u, wire::encodeIdle(0, 8, idle, sizeof(idle)));
  TEST_ASSERT_FALSE_MESSAGE(wire::decode(idle, 10, packet), "an idle clock without a size");
  uint8_t control[16];
  TEST_ASSERT_EQUAL(10u, wire::encodeSubscribe(52, 16, control, sizeof(control)));
  TEST_ASSERT_TRUE(wire::decode(control, 10, packet));
  TEST_ASSERT_EQUAL(static_cast<int>(wire::Type::Subscribe), static_cast<int>(packet.type));
  TEST_ASSERT_EQUAL(52, packet.width);
  TEST_ASSERT_EQUAL(16, packet.height);
  TEST_ASSERT_FALSE(wire::decode(control, 6, packet));
  TEST_ASSERT_EQUAL(10u, wire::encodeSubscribe(0, 16, control, sizeof(control)));
  TEST_ASSERT_FALSE_MESSAGE(wire::decode(control, 10, packet), "a viewer without a size");
  TEST_ASSERT_EQUAL(6u, wire::encodeLeave(control, sizeof(control)));
  TEST_ASSERT_TRUE(wire::decode(control, 6, packet));
  TEST_ASSERT_FALSE(wire::decode(control, 7, packet));
  TEST_ASSERT_EQUAL(0u, wire::encodeLeave(control, 5));
}

static void test_every_panel_size_fits_the_datagram() {
  wire::FrameHeader header;
  const std::string longest(300, 'x');
  header.app = longest;
  header.incoming = longest;
  TEST_ASSERT_TRUE(wire::rowsPerDatagram(128, header) >= 1);
  TEST_ASSERT_TRUE_MESSAGE(wire::rowsPerDatagram(32, wire::FrameHeader{}) >= 8,
                           "a 32x8 frame goes out as one datagram");
}

static void test_the_filter_reads_the_app_list() {
  TEST_ASSERT_TRUE(Filter().admits(app("Time")));
  TEST_ASSERT_TRUE(Filter().admits(notification()));
  TEST_ASSERT_TRUE(Filter("*", false).admits(app("anything")));
  TEST_ASSERT_FALSE(Filter("*", false).admits(notification()));
  TEST_ASSERT_FALSE_MESSAGE(Filter("", true).admits(app("Time")), "an empty list admits no app");
  TEST_ASSERT_TRUE(Filter("", true).admits(notification()));

  const Filter some(" weather , Time,,", false);
  TEST_ASSERT_TRUE(some.admits(app("Weather")));
  TEST_ASSERT_TRUE(some.admits(app("TIME")));
  TEST_ASSERT_FALSE(some.admits(app("Date")));
  TEST_ASSERT_TRUE_MESSAGE(some.admits(app("Date", "Weather")), "the app sliding in counts");
  TEST_ASSERT_FALSE(some.admits(notification()));

  PageInfo external;
  external.kind = PageKind::External;
  TEST_ASSERT_FALSE_MESSAGE(Filter().admits(external), "a mirrored display is never passed on");
}

static void test_the_source_sends_only_what_changed_and_a_keepalive() {
  Recorder out;
  Source source(out, Filter(), 32, 8);
  Canvas canvas(32, 8);
  const PageInfo page = app("Time");
  const net::Endpoint viewer{0x0A000002u, wire::kPort};
  source.publish(canvas, &page, 0);
  TEST_ASSERT_EQUAL_MESSAGE(0, out.sent.size(), "nobody is watching");

  TEST_ASSERT_TRUE(source.subscribe(viewer, 32, 8, 0));
  source.publish(canvas, &page, 10);
  TEST_ASSERT_EQUAL(1, out.count(wire::Type::Frame));
  TEST_ASSERT_TRUE(out.sent[0].to == viewer);
  source.publish(canvas, &page, 40);
  TEST_ASSERT_EQUAL_MESSAGE(1, out.count(wire::Type::Frame), "unchanged frames are not repeated");
  canvas.setPixel(3, 3, 0x123456u);
  source.publish(canvas, &page, 70);
  TEST_ASSERT_EQUAL(2, out.count(wire::Type::Frame));
  source.publish(canvas, &page, 1069);
  TEST_ASSERT_EQUAL(2, out.count(wire::Type::Frame));
  source.publish(canvas, &page, 1070);
  TEST_ASSERT_EQUAL_MESSAGE(3, out.count(wire::Type::Frame), "a keepalive after one second");
  const PageInfo other = app("Date");
  source.publish(canvas, &other, 1100);
  TEST_ASSERT_EQUAL_MESSAGE(4, out.count(wire::Type::Frame), "a new page is a change");
}

static void test_the_source_says_idle_once_and_then_only_as_keepalive() {
  Recorder out;
  Source source(out, Filter("Time", false), 32, 8);
  Canvas canvas(32, 8);
  const net::Endpoint viewer{0x0A000002u, wire::kPort};
  source.subscribe(viewer, 32, 8, 0);
  const PageInfo time = app("Time");
  const PageInfo date = app("Date");
  source.publish(canvas, &time, 0);
  source.publish(canvas, &date, 20);
  TEST_ASSERT_EQUAL(1, out.count(wire::Type::Idle));
  source.publish(canvas, &date, 40);
  source.publish(canvas, nullptr, 60);
  TEST_ASSERT_EQUAL(1, out.count(wire::Type::Idle));
  source.publish(canvas, nullptr, 1020);
  TEST_ASSERT_EQUAL(2, out.count(wire::Type::Idle));
  source.publish(canvas, &time, 1040);
  TEST_ASSERT_EQUAL_MESSAGE(2, out.count(wire::Type::Frame), "sharing resumes at once");
  const std::vector<wire::Packet> packets = out.packets();
  TEST_ASSERT_EQUAL(32, packets[1].width);
  TEST_ASSERT_EQUAL(8, packets[1].height);
}

static void test_viewers_are_bounded_and_leases_expire() {
  Recorder out;
  Source source(out, Filter(), 32, 8);
  for (uint32_t i = 0; i < Source::kMaxViewers; ++i)
    TEST_ASSERT_TRUE(source.subscribe({0x0A000010u + i, wire::kPort}, 32, 8, 0));
  TEST_ASSERT_FALSE(source.subscribe({0x0A0000FFu, wire::kPort}, 32, 8, 0));
  TEST_ASSERT_TRUE_MESSAGE(source.subscribe({0x0A000010u, wire::kPort}, 32, 8, 3000), "renewal");
  TEST_ASSERT_EQUAL(Source::kMaxViewers, source.viewers());
  source.leave({0x0A000011u, wire::kPort});
  TEST_ASSERT_EQUAL(Source::kMaxViewers - 1, source.viewers());
  source.expire(Source::kLeaseMs);
  TEST_ASSERT_EQUAL_MESSAGE(1, source.viewers(), "only the renewed lease is left");
  source.expire(3000 + Source::kLeaseMs);
  TEST_ASSERT_EQUAL(0, source.viewers());
}

static void test_a_viewer_of_another_size_only_learns_the_size() {
  Recorder out;
  Source source(out, Filter(), 32, 8);
  const net::Endpoint viewer{0x0A000002u, wire::kPort};
  TEST_ASSERT_FALSE(source.subscribe(viewer, 52, 16, 0));
  TEST_ASSERT_EQUAL(0, source.viewers());
  const std::vector<wire::Packet> packets = out.packets();
  TEST_ASSERT_EQUAL(1, packets.size());
  TEST_ASSERT_EQUAL(static_cast<int>(wire::Type::Idle), static_cast<int>(packets[0].type));
  TEST_ASSERT_EQUAL(32, packets[0].width);
  TEST_ASSERT_TRUE(out.sent[0].to == viewer);
  Canvas canvas(32, 8);
  const PageInfo page = app("Time");
  source.publish(canvas, &page, 10);
  TEST_ASSERT_EQUAL_MESSAGE(1, out.sent.size(), "it gets no frames");
}

static void test_a_follower_shows_the_shared_display() {
  Pair p;
  TEST_ASSERT_FALSE(p.b.mirror.active());
  frame(p.a, p.b, 0);
  TEST_ASSERT_TRUE(p.b.mirror.active());
  TEST_ASSERT_EQUAL_STRING("showing", followStateName(p.b.status.follow));
  TEST_ASSERT_EQUAL(1, p.a.status.viewers);
  TEST_ASSERT_TRUE(p.a.status.sharing);
  p.b.mirror.draw(p.b.panel);
  TEST_ASSERT_TRUE(same(p.a.panel, p.b.panel));

  paint(p.a.panel, 2);
  frame(p.a, p.b, 25);
  p.b.mirror.draw(p.b.panel);
  TEST_ASSERT_TRUE(same(p.a.panel, p.b.panel));
}

static void test_a_wide_frame_is_shown_only_once_every_row_is_in() {
  Node a(0x0A000001u, 128, 32);
  Node b(0x0A000002u, 128, 32);
  a.mirror.configure(sharing());
  b.mirror.configure(following("a"));
  b.mirror.setSource(a.self);
  paint(a.panel, 3);
  frame(a, b, 0);
  TEST_ASSERT_TRUE(b.mirror.active());
  Canvas first(128, 32);
  b.mirror.draw(first);
  TEST_ASSERT_TRUE(same(a.panel, first));

  paint(a.panel, 4);
  b.mirror.tick(20);
  a.mirror.content(a.panel, &a.page, 20);
  TEST_ASSERT_TRUE(a.out.sent.size() > 1);
  deliver(a, b, 20, [](std::size_t i, const Sent&) { return i == 1; });
  b.mirror.draw(b.panel);
  TEST_ASSERT_TRUE_MESSAGE(same(first, b.panel), "a frame missing a datagram is never shown");

  paint(a.panel, 5);
  frame(a, b, 40);
  b.mirror.draw(b.panel);
  TEST_ASSERT_TRUE(same(a.panel, b.panel));
}

static void test_the_follower_filter_hands_the_panel_back() {
  Pair p(32, 8, following("a", "Weather", false));
  frame(p.a, p.b, 0);
  TEST_ASSERT_FALSE(p.b.mirror.active());
  TEST_ASSERT_EQUAL_STRING("filtered", followStateName(p.b.status.follow));
  p.a.page = app("weather");
  frame(p.a, p.b, 20);
  TEST_ASSERT_TRUE(p.b.mirror.active());
  p.a.page = notification();
  frame(p.a, p.b, 40);
  TEST_ASSERT_FALSE(p.b.mirror.active());
}

static void test_the_shared_filter_sends_idle_instead() {
  Pair p;
  p.a.mirror.configure(sharing("Weather", true));
  frame(p.a, p.b, 0);
  TEST_ASSERT_FALSE(p.b.mirror.active());
  TEST_ASSERT_EQUAL_STRING("idle", followStateName(p.b.status.follow));
  p.a.page = notification();
  frame(p.a, p.b, 20);
  TEST_ASSERT_TRUE(p.b.mirror.active());
  p.a.hasPage = false;
  frame(p.a, p.b, 40);
  TEST_ASSERT_FALSE_MESSAGE(p.b.mirror.active(), "a dark or taken-over display is not shared");
}

static void test_different_panel_sizes_are_not_mirrored() {
  Pair p(52, 16);
  frame(p.a, p.b, 0);
  TEST_ASSERT_FALSE(p.b.mirror.active());
  TEST_ASSERT_EQUAL_STRING("sizeMismatch", followStateName(p.b.status.follow));
  TEST_ASSERT_EQUAL(32, p.b.status.sourceWidth);
  TEST_ASSERT_EQUAL(8, p.b.status.sourceHeight);
  TEST_ASSERT_EQUAL_MESSAGE(0, p.a.status.viewers, "it takes no place");
  for (int64_t t = 25; t <= 8000; t += 25) frame(p.a, p.b, t);
  TEST_ASSERT_EQUAL_STRING_MESSAGE("sizeMismatch", followStateName(p.b.status.follow),
                                   "the answers to its subscriptions keep the state");
}

static void test_silence_hands_the_panel_back() {
  Pair p;
  frame(p.a, p.b, 0);
  TEST_ASSERT_TRUE(p.b.mirror.active());
  p.b.mirror.tick(Receiver::kSilenceMs);
  TEST_ASSERT_TRUE(p.b.mirror.active());
  p.b.mirror.tick(Receiver::kSilenceMs + 1);
  TEST_ASSERT_FALSE(p.b.mirror.active());
  TEST_ASSERT_EQUAL_STRING("waiting", followStateName(p.b.status.follow));
}

static void test_the_follower_renews_and_the_source_forgets_a_silent_viewer() {
  Pair p;
  for (int64_t t = 0; t <= 10000; t += 25) frame(p.a, p.b, t);
  TEST_ASSERT_TRUE(p.b.mirror.active());
  TEST_ASSERT_EQUAL(1, p.a.status.viewers);
  p.b.mirror.configure(Config{});
  deliver(p.b, p.a, 10025);
  TEST_ASSERT_EQUAL_MESSAGE(0, p.a.mirror.status().viewers, "leaving is said, not waited out");
  TEST_ASSERT_FALSE(p.b.mirror.active());
  TEST_ASSERT_EQUAL_STRING("off", followStateName(p.b.status.follow));
}

static void test_datagrams_from_other_clocks_are_ignored() {
  Pair p;
  Node stranger(0x0A000009u);
  stranger.mirror.configure(sharing());
  stranger.mirror.receive(p.b.self, nullptr, 0, 0);
  uint8_t subscribe[16];
  const std::size_t length = wire::encodeSubscribe(32, 8, subscribe, sizeof(subscribe));
  stranger.mirror.receive(p.b.self, subscribe, length, 0);
  TEST_ASSERT_EQUAL(1, stranger.status.viewers);
  stranger.mirror.content(stranger.panel, &stranger.page, 0);
  deliver(stranger, p.b, 0);
  TEST_ASSERT_FALSE(p.b.mirror.active());
}

static void test_a_mirrored_display_is_not_passed_on() {
  Node a(0x0A000001u), b(0x0A000002u), c(0x0A000003u);
  a.mirror.configure(sharing());
  Config middle = sharing();
  middle.follow = "a";
  b.mirror.configure(middle);
  b.mirror.setSource(a.self);
  c.mirror.configure(following("b"));
  c.mirror.setSource(b.self);
  frame(a, b, 0);
  TEST_ASSERT_TRUE(b.mirror.active());
  PageInfo external;
  external.kind = PageKind::External;
  b.page = external;
  frame(b, c, 0);
  TEST_ASSERT_FALSE(c.mirror.active());
  TEST_ASSERT_EQUAL_STRING("idle", followStateName(c.status.follow));
}

static void test_going_offline_forgets_everything_quietly() {
  Pair p;
  frame(p.a, p.b, 0);
  p.a.mirror.setOnline(false);
  p.b.mirror.setOnline(false);
  TEST_ASSERT_FALSE(p.b.mirror.active());
  TEST_ASSERT_EQUAL_STRING("offline", followStateName(p.b.status.follow));
  TEST_ASSERT_FALSE(p.a.status.sharing);
  TEST_ASSERT_EQUAL(0, p.a.status.viewers);
  p.a.out.sent.clear();
  p.b.out.sent.clear();
  p.a.mirror.content(p.a.panel, &p.a.page, 10);
  p.b.mirror.tick(10);
  TEST_ASSERT_EQUAL(0, p.a.out.sent.size() + p.b.out.sent.size());
}

static void test_the_follower_reports_its_lookup() {
  Node b(0x0A000002u);
  b.mirror.configure(following("clock.local"));
  TEST_ASSERT_EQUAL_STRING("resolving", followStateName(b.status.follow));
  TEST_ASSERT_EQUAL_STRING("clock.local", b.status.source.c_str());
  b.mirror.setLookup(FollowState::NotFound);
  TEST_ASSERT_EQUAL_STRING("notFound", followStateName(b.status.follow));
  b.mirror.setSource({0x0A000001u, wire::kPort});
  TEST_ASSERT_EQUAL_STRING("waiting", followStateName(b.status.follow));
  b.mirror.tick(0);
  TEST_ASSERT_EQUAL(1, b.out.count(wire::Type::Subscribe));
  b.mirror.setSource({0x0A000005u, wire::kPort});
  TEST_ASSERT_EQUAL_MESSAGE(1, b.out.count(wire::Type::Leave), "the old address is left");
}

int main(int, char**) {
  UNITY_BEGIN();
  RUN_TEST(test_a_frame_survives_the_wire);
  RUN_TEST(test_malformed_datagrams_are_refused);
  RUN_TEST(test_every_panel_size_fits_the_datagram);
  RUN_TEST(test_the_filter_reads_the_app_list);
  RUN_TEST(test_the_source_sends_only_what_changed_and_a_keepalive);
  RUN_TEST(test_the_source_says_idle_once_and_then_only_as_keepalive);
  RUN_TEST(test_viewers_are_bounded_and_leases_expire);
  RUN_TEST(test_a_viewer_of_another_size_only_learns_the_size);
  RUN_TEST(test_a_follower_shows_the_shared_display);
  RUN_TEST(test_a_wide_frame_is_shown_only_once_every_row_is_in);
  RUN_TEST(test_the_follower_filter_hands_the_panel_back);
  RUN_TEST(test_the_shared_filter_sends_idle_instead);
  RUN_TEST(test_different_panel_sizes_are_not_mirrored);
  RUN_TEST(test_silence_hands_the_panel_back);
  RUN_TEST(test_the_follower_renews_and_the_source_forgets_a_silent_viewer);
  RUN_TEST(test_datagrams_from_other_clocks_are_ignored);
  RUN_TEST(test_a_mirrored_display_is_not_passed_on);
  RUN_TEST(test_going_offline_forgets_everything_quietly);
  RUN_TEST(test_the_follower_reports_its_lookup);
  return UNITY_END();
}
