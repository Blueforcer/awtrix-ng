#include <unity.h>

#include <cstring>
#include <string>

#include "core/remote/RemoteWire.h"

using namespace awtrix;
using namespace awtrix::remote;

namespace {

uint8_t buf[wire::kMaxDatagram];

void fakeSign(const uint8_t* data, std::size_t length, uint8_t* tag) {
  uint8_t sum = 0;
  for (std::size_t i = 0; i < length; ++i) sum = static_cast<uint8_t>(sum + data[i]);
  for (std::size_t i = 0; i < wire::kTagBytes; ++i) tag[i] = static_cast<uint8_t>(sum + i);
}

wire::Packet decoded(std::size_t length) {
  wire::Packet p;
  TEST_ASSERT_TRUE(wire::decode(buf, length, p));
  return p;
}

}

void setUp() { std::memset(buf, 0, sizeof(buf)); }
void tearDown() {}

static void test_hello_round_trips_and_exposes_its_tag() {
  wire::Hello h;
  h.uid = "a1b2c3d4e5f6";
  h.width = 32;
  h.height = 8;
  h.address = 0xC0A8B214u;
  h.webPort = 8080;
  h.counter = 70000;
  h.settingsRevision = 9;
  h.systemRevision = 65535;
  h.assetsRevision = 3;
  h.appsRevision = 4321;
  h.offMask = 0x80000005u;
  h.state = wire::state::kDisplayOn;
  h.sensors.present = wire::sensor::kTemperature | wire::sensor::kBattery;
  h.sensors.temperatureCenti = -1250;
  h.sensors.batteryPercent = 87;
  h.sensors.batteryMillivolts = 4012;
  const std::size_t n = wire::encodeHello(h, fakeSign, buf, sizeof(buf));
  TEST_ASSERT_GREATER_THAN(wire::kTagBytes, n);
  const wire::Packet p = decoded(n);
  TEST_ASSERT_EQUAL(static_cast<int>(wire::Type::Hello), static_cast<int>(p.type));
  TEST_ASSERT_EQUAL_STRING_LEN("a1b2c3d4e5f6", p.hello.uid.data(), p.hello.uid.size());
  TEST_ASSERT_EQUAL(32, p.hello.width);
  TEST_ASSERT_EQUAL(8, p.hello.height);
  TEST_ASSERT_EQUAL_HEX32(0xC0A8B214u, p.hello.address);
  TEST_ASSERT_EQUAL_UINT16(8080, p.hello.webPort);
  TEST_ASSERT_EQUAL_UINT32(70000, p.hello.counter);
  TEST_ASSERT_EQUAL_UINT16(9, p.hello.settingsRevision);
  TEST_ASSERT_EQUAL_UINT16(65535, p.hello.systemRevision);
  TEST_ASSERT_EQUAL_UINT16(3, p.hello.assetsRevision);
  TEST_ASSERT_EQUAL_UINT16(4321, p.hello.appsRevision);
  TEST_ASSERT_EQUAL_HEX32(0x80000005u, p.hello.offMask);
  TEST_ASSERT_EQUAL(wire::state::kDisplayOn, p.hello.state);
  TEST_ASSERT_EQUAL(-1250, p.hello.sensors.temperatureCenti);
  TEST_ASSERT_EQUAL(87, p.hello.sensors.batteryPercent);
  TEST_ASSERT_EQUAL(4012, p.hello.sensors.batteryMillivolts);
  TEST_ASSERT_EQUAL(n - wire::kTagBytes, p.signedBytes);
  uint8_t expected[wire::kTagBytes];
  fakeSign(buf, p.signedBytes, expected);
  TEST_ASSERT_EQUAL_MEMORY(expected, p.tag, wire::kTagBytes);
}

static void test_a_hello_without_signer_or_uid_is_not_written() {
  wire::Hello h;
  h.uid = "abc";
  h.width = 32;
  h.height = 8;
  TEST_ASSERT_EQUAL(0, wire::encodeHello(h, nullptr, buf, sizeof(buf)));
  h.uid = "";
  TEST_ASSERT_EQUAL(0, wire::encodeHello(h, fakeSign, buf, sizeof(buf)));
}

static void test_show_round_trips() {
  wire::Show s;
  s.sequence = 513;
  s.buttonsDown = 0b101;
  s.presses[0] = 7;
  s.presses[2] = 255;
  s.current = "Weather";
  s.incoming = "Stocks";
  const wire::Packet p = decoded(wire::encodeShow(s, buf, sizeof(buf)));
  TEST_ASSERT_EQUAL(513, p.show.sequence);
  TEST_ASSERT_EQUAL(0b101, p.show.buttonsDown);
  TEST_ASSERT_EQUAL(7, p.show.presses[0]);
  TEST_ASSERT_EQUAL(0, p.show.presses[1]);
  TEST_ASSERT_EQUAL(255, p.show.presses[2]);
  TEST_ASSERT_EQUAL_STRING_LEN("Weather", p.show.current.data(), p.show.current.size());
  TEST_ASSERT_EQUAL_STRING_LEN("Stocks", p.show.incoming.data(), p.show.incoming.size());
}

static void test_apps_round_trip_up_to_the_limit() {
  wire::Apps a;
  static char names[wire::kMaxApps][wire::kMaxName + 1];
  a.revision = 4;
  a.count = wire::kMaxApps;
  for (std::size_t i = 0; i < wire::kMaxApps; ++i) {
    std::memset(names[i], 'a' + static_cast<int>(i % 26), wire::kMaxName);
    names[i][wire::kMaxName] = '\0';
    a.entries[i].name = names[i];
    a.entries[i].flags = wire::app::kWantsShow;
    a.entries[i].durationMs = 10000 + static_cast<uint32_t>(i);
  }
  const std::size_t n = wire::encodeApps(a, buf, sizeof(buf));
  TEST_ASSERT_GREATER_THAN(0, n);
  TEST_ASSERT_LESS_OR_EQUAL(wire::kMaxDatagram, n);
  const wire::Packet p = decoded(n);
  TEST_ASSERT_EQUAL(4, p.apps.revision);
  TEST_ASSERT_EQUAL(wire::kMaxApps, p.apps.count);
  TEST_ASSERT_EQUAL_UINT32(10031, p.apps.entries[31].durationMs);
  TEST_ASSERT_EQUAL(wire::kMaxName, p.apps.entries[31].name.size());
}

static void test_a_frame_survives_the_wire() {
  Canvas c(32, 8);
  c.clear(0x102030);
  c.setPixel(31, 7, 0xFFEEDD);
  const std::size_t n = wire::encodeFrame(c, "Weather", 42, 0, 8, buf, sizeof(buf));
  TEST_ASSERT_EQUAL(6 + 10 + 1 + 7 + 32 * 8 * 3, n);
  const wire::Packet p = decoded(n);
  TEST_ASSERT_EQUAL(42, p.frame.stampMs);
  TEST_ASSERT_EQUAL(32, p.frame.width);
  TEST_ASSERT_EQUAL(8, p.frame.rows);
  TEST_ASSERT_EQUAL_HEX8(0x10, p.frame.rgb[0]);
  TEST_ASSERT_EQUAL_HEX8(0x30, p.frame.rgb[2]);
  const uint8_t* last = p.frame.rgb + (32 * 8 - 1) * 3;
  TEST_ASSERT_EQUAL_HEX8(0xFF, last[0]);
  TEST_ASSERT_EQUAL_HEX8(0xDD, last[2]);
}

static void test_every_panel_width_fits_with_the_longest_name() {
  const std::string longest(wire::kMaxName, 'x');
  for (int width = 32; width <= 128; width += 8) {
    Canvas c(width, 8);
    const int step = wire::rowsPerDatagram(width, longest);
    TEST_ASSERT_GREATER_THAN(0, step);
    for (int row = 0; row < 8; row += step) {
      const int rows = step < 8 - row ? step : 8 - row;
      TEST_ASSERT_GREATER_THAN(0, wire::encodeFrame(c, longest, 1, row, rows, buf, sizeof(buf)));
    }
  }
}

static void test_a_plain_offer_round_trips_without_a_tag() {
  wire::Offer o;
  o.name = "raspberrypi";
  o.port = 8300;
  const wire::Packet p = decoded(wire::encodeOffer(o, buf, sizeof(buf)));
  TEST_ASSERT_EQUAL(8300, p.offer.port);
  TEST_ASSERT_EQUAL_STRING_LEN("raspberrypi", p.offer.name.data(), p.offer.name.size());
  TEST_ASSERT_EQUAL(0, p.offer.uid.size());
  TEST_ASSERT_NULL(p.tag);
}

static void test_a_signed_offer_names_its_clock_and_carries_a_tag() {
  wire::Offer o;
  o.name = "raspberrypi";
  o.port = 8302;
  o.uid = "a1b2c3d4e5f6";
  o.unixTime = 1790000000u;
  TEST_ASSERT_EQUAL_MESSAGE(0, wire::encodeOffer(o, buf, sizeof(buf)), "a signed offer needs a signer");
  const std::size_t n = wire::encodeOffer(o, buf, sizeof(buf), fakeSign);
  const wire::Packet p = decoded(n);
  TEST_ASSERT_EQUAL(static_cast<int>(wire::Type::Offer), static_cast<int>(p.type));
  TEST_ASSERT_EQUAL(8302, p.offer.port);
  TEST_ASSERT_EQUAL_STRING_LEN("a1b2c3d4e5f6", p.offer.uid.data(), p.offer.uid.size());
  TEST_ASSERT_EQUAL_UINT32(1790000000u, p.offer.unixTime);
  TEST_ASSERT_EQUAL(n - wire::kTagBytes, p.signedBytes);
  uint8_t expected[wire::kTagBytes];
  fakeSign(buf, p.signedBytes, expected);
  TEST_ASSERT_EQUAL_MEMORY(expected, p.tag, wire::kTagBytes);
  wire::Packet cut;
  TEST_ASSERT_FALSE_MESSAGE(wire::decode(buf, n - 1, cut), "a short tag");
  TEST_ASSERT_FALSE_MESSAGE(wire::decode(buf, n - wire::kTagBytes, cut), "no tag");
  o.name = std::string_view();
  TEST_ASSERT_EQUAL(0, wire::encodeOffer(o, buf, sizeof(buf), fakeSign));
}

static void test_the_retired_type_five_is_refused() {
  const uint8_t bye[] = {'A', 'W', 'R', 'H', wire::kVersion, 5};
  wire::Packet p;
  TEST_ASSERT_FALSE(wire::decode(bye, sizeof(bye), p));
}

static void test_malformed_datagrams_are_refused() {
  wire::Packet p;
  wire::Show s;
  s.current = "Weather";
  const std::size_t n = wire::encodeShow(s, buf, sizeof(buf));
  TEST_ASSERT_FALSE(wire::decode(buf, n - 1, p));
  TEST_ASSERT_FALSE(wire::decode(buf, n + 1, p));
  buf[4] = 2;
  TEST_ASSERT_FALSE_MESSAGE(wire::decode(buf, n, p), "another version");
  buf[4] = wire::kVersion;
  buf[0] = 'X';
  TEST_ASSERT_FALSE_MESSAGE(wire::decode(buf, n, p), "another magic");
  buf[0] = 'A';
  buf[5] = 99;
  TEST_ASSERT_FALSE_MESSAGE(wire::decode(buf, n, p), "unknown type");
  buf[5] = static_cast<uint8_t>(wire::Type::Show);
  buf[8] = 0b1000;
  TEST_ASSERT_FALSE_MESSAGE(wire::decode(buf, n, p), "a fourth button");
  const std::string tooLong(wire::kMaxName + 1, 'x');
  s.current = tooLong;
  TEST_ASSERT_EQUAL_MESSAGE(0, wire::encodeShow(s, buf, sizeof(buf)), "names longer than the limit");
  Canvas c(32, 8);
  const std::size_t f = wire::encodeFrame(c, "A", 1, 4, 4, buf, sizeof(buf));
  buf[6 + 6] = 6;
  TEST_ASSERT_FALSE_MESSAGE(wire::decode(buf, f, p), "rows outside the frame");
  TEST_ASSERT_FALSE(wire::decode(nullptr, 10, p));
}

static void test_apps_over_the_limit_are_refused() {
  wire::Apps a;
  a.count = wire::kMaxApps;
  for (std::size_t i = 0; i < wire::kMaxApps; ++i) a.entries[i].name = "A";
  const std::size_t n = wire::encodeApps(a, buf, sizeof(buf));
  buf[6 + 2] = static_cast<uint8_t>(wire::kMaxApps + 1);
  wire::Packet p;
  TEST_ASSERT_FALSE(wire::decode(buf, n, p));
}

int main(int, char**) {
  UNITY_BEGIN();
  RUN_TEST(test_hello_round_trips_and_exposes_its_tag);
  RUN_TEST(test_a_hello_without_signer_or_uid_is_not_written);
  RUN_TEST(test_show_round_trips);
  RUN_TEST(test_apps_round_trip_up_to_the_limit);
  RUN_TEST(test_a_frame_survives_the_wire);
  RUN_TEST(test_every_panel_width_fits_with_the_longest_name);
  RUN_TEST(test_a_plain_offer_round_trips_without_a_tag);
  RUN_TEST(test_a_signed_offer_names_its_clock_and_carries_a_tag);
  RUN_TEST(test_the_retired_type_five_is_refused);
  RUN_TEST(test_malformed_datagrams_are_refused);
  RUN_TEST(test_apps_over_the_limit_are_refused);
  return UNITY_END();
}
