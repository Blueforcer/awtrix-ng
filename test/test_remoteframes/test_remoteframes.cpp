#include <unity.h>

#include <string>

#include "core/FrameClock.h"
#include "core/remote/RemoteFrames.h"
#include "core/remote/RemoteWire.h"

using namespace awtrix;
using namespace awtrix::remote;

namespace {

uint8_t buf[wire::kMaxDatagram];

wire::Frame part(const Canvas& c, const char* app, uint16_t stampMs, int firstRow, int rows) {
  const std::size_t n = wire::encodeFrame(c, app, stampMs, firstRow, rows, buf, sizeof(buf));
  wire::Packet p;
  TEST_ASSERT_TRUE(wire::decode(buf, n, p));
  return p.frame;
}

Canvas filled(int width, int height, uint32_t rgb) {
  Canvas c(width, height);
  c.clear(rgb);
  return c;
}

}

void setUp() {}
void tearDown() {}

static void test_a_complete_frame_is_drawn() {
  RemoteFrames f;
  f.begin(32, 8);
  f.want("Weather", "");
  Canvas src = filled(32, 8, 0x112233);
  src.setPixel(5, 3, 0xABCDEF);
  f.receive(part(src, "Weather", 1, 0, 8), 0);
  Canvas out(32, 8);
  TEST_ASSERT_TRUE(f.draw("Weather", out));
  TEST_ASSERT_EQUAL_HEX32(0x112233, out.getPixel(0, 0));
  TEST_ASSERT_EQUAL_HEX32(0xABCDEF, out.getPixel(5, 3));
}

static void test_nothing_is_drawn_before_the_first_frame() {
  RemoteFrames f;
  f.begin(32, 8);
  f.want("Weather", "");
  Canvas out = filled(32, 8, 0x010101);
  TEST_ASSERT_FALSE(f.draw("Weather", out));
  TEST_ASSERT_EQUAL_HEX32(0x010101, out.getPixel(0, 0));
}

static void test_a_split_frame_shows_only_once_every_row_is_in() {
  RemoteFrames f;
  f.begin(32, 8);
  f.want("Weather", "");
  f.receive(part(filled(32, 8, 0x0000FF), "Weather", 0, 0, 8), 0);
  const Canvas next = filled(32, 8, 0xFF0000);
  f.receive(part(next, "Weather", 24, 0, 4), 24);
  f.play(500);
  Canvas out(32, 8);
  TEST_ASSERT_TRUE(f.draw("Weather", out));
  TEST_ASSERT_EQUAL_HEX32_MESSAGE(0x0000FF, out.getPixel(0, 7), "the old frame stays until the new one is whole");
  f.receive(part(next, "Weather", 24, 4, 4), 24);
  f.play(500);
  TEST_ASSERT_TRUE(f.draw("Weather", out));
  TEST_ASSERT_EQUAL_HEX32(0xFF0000, out.getPixel(0, 0));
  TEST_ASSERT_EQUAL_HEX32(0xFF0000, out.getPixel(0, 7));
}

static void test_a_new_stamp_drops_a_half_frame() {
  RemoteFrames f;
  f.begin(32, 8);
  f.want("Weather", "");
  f.receive(part(filled(32, 8, 0x00FF00), "Weather", 1, 0, 4), 0);
  f.receive(part(filled(32, 8, 0xFF0000), "Weather", 2, 4, 4), 0);
  Canvas out(32, 8);
  TEST_ASSERT_FALSE_MESSAGE(f.draw("Weather", out), "rows of two frames never make one");
}

static void test_frames_of_apps_not_wanted_are_ignored() {
  RemoteFrames f;
  f.begin(32, 8);
  f.want("Weather", "");
  f.receive(part(filled(32, 8, 0xFFFFFF), "Stocks", 1, 0, 8), 0);
  Canvas out(32, 8);
  TEST_ASSERT_FALSE(f.draw("Stocks", out));
  TEST_ASSERT_FALSE(f.draw("Weather", out));
}

static void test_both_wanted_apps_keep_their_own_picture() {
  RemoteFrames f;
  f.begin(32, 8);
  f.want("Weather", "Stocks");
  f.receive(part(filled(32, 8, 0x0000FF), "Weather", 1, 0, 8), 0);
  f.receive(part(filled(32, 8, 0x00FF00), "Stocks", 1, 0, 8), 0);
  Canvas out(32, 8);
  TEST_ASSERT_TRUE(f.draw("Weather", out));
  TEST_ASSERT_EQUAL_HEX32(0x0000FF, out.getPixel(0, 0));
  TEST_ASSERT_TRUE(f.draw("Stocks", out));
  TEST_ASSERT_EQUAL_HEX32(0x00FF00, out.getPixel(0, 0));
}

static void test_wanting_other_apps_frees_the_old_pictures() {
  RemoteFrames f;
  f.begin(32, 8);
  f.want("Weather", "Stocks");
  f.receive(part(filled(32, 8, 0x0000FF), "Weather", 1, 0, 8), 0);
  f.receive(part(filled(32, 8, 0x00FF00), "Stocks", 1, 0, 8), 0);
  f.want("Stocks", "");
  Canvas out(32, 8);
  TEST_ASSERT_TRUE_MESSAGE(f.draw("Stocks", out), "the app that stays keeps its picture");
  TEST_ASSERT_FALSE(f.draw("Weather", out));
  f.want("Weather", "");
  TEST_ASSERT_FALSE_MESSAGE(f.draw("Weather", out), "a picture once dropped does not come back");
  f.want("", "");
  TEST_ASSERT_FALSE(f.draw("Stocks", out));
}

static void test_a_frame_of_another_size_is_ignored() {
  RemoteFrames f;
  f.begin(32, 8);
  f.want("Weather", "");
  f.receive(part(filled(40, 8, 0xFFFFFF), "Weather", 1, 0, 8), 0);
  Canvas out(32, 8);
  TEST_ASSERT_FALSE(f.draw("Weather", out));
}

static void test_a_wide_frame_in_three_parts_shows_whole() {
  RemoteFrames f;
  f.begin(128, 8);
  f.want("Game", "");
  const Canvas src = filled(128, 8, 0x123456);
  Canvas out(128, 8);
  f.receive(part(src, "Game", 1, 0, 3), 0);
  f.receive(part(src, "Game", 1, 3, 3), 0);
  TEST_ASSERT_FALSE(f.draw("Game", out));
  f.receive(part(src, "Game", 1, 6, 2), 0);
  TEST_ASSERT_TRUE(f.draw("Game", out));
  TEST_ASSERT_EQUAL_HEX32(0x123456, out.getPixel(127, 7));
}

static uint32_t shown(RemoteFrames& f, int64_t nowMs) {
  f.play(nowMs);
  Canvas out(32, 8);
  TEST_ASSERT_TRUE(f.draw("Game", out));
  return out.getPixel(0, 0);
}

static void send(RemoteFrames& f, uint16_t stampMs, int64_t nowMs) {
  f.receive(part(filled(32, 8, stampMs), "Game", stampMs, 0, 8), nowMs);
}

static void test_a_frame_waits_for_its_time() {
  RemoteFrames f;
  f.begin(32, 8);
  f.want("Game", "");
  send(f, 0, 100);
  TEST_ASSERT_EQUAL_HEX32_MESSAGE(0, shown(f, 100), "the first frame shows at once");
  send(f, 24, 124);
  TEST_ASSERT_EQUAL_HEX32(0, shown(f, 124));
  TEST_ASSERT_EQUAL_HEX32(24, shown(f, 124 + f.delayMs()));
}

static void test_a_burst_is_played_out_frame_by_frame() {
  RemoteFrames f;
  f.begin(32, 8);
  f.want("Game", "");
  for (int i = 0; i <= 4; ++i) send(f, static_cast<uint16_t>(i * 24), 100 + i * 24);
  for (uint16_t stamp : {120, 144, 168}) send(f, stamp, 300);
  const int64_t delay = f.delayMs();
  TEST_ASSERT_TRUE_MESSAGE(delay >= 80, "the delay covers the late frames");
  TEST_ASSERT_EQUAL_HEX32(120, shown(f, 100 + 120 + delay));
  TEST_ASSERT_EQUAL_HEX32(144, shown(f, 100 + 144 + delay));
  TEST_ASSERT_EQUAL_HEX32(168, shown(f, 100 + 168 + delay));
}

static void test_a_late_frame_never_steps_back() {
  RemoteFrames f;
  f.begin(32, 8);
  f.want("Game", "");
  send(f, 0, 100);
  send(f, 24, 124);
  TEST_ASSERT_EQUAL_HEX32(24, shown(f, 400));
  send(f, 12, 410);
  TEST_ASSERT_EQUAL_HEX32(24, shown(f, 800));
}

static void test_the_delay_has_a_ceiling() {
  RemoteFrames f;
  f.begin(32, 8);
  f.want("Game", "");
  send(f, 0, 100);
  for (int i = 1; i < 20; ++i) send(f, static_cast<uint16_t>(i * 24), 100 + i * 24 + (i % 2) * 600);
  TEST_ASSERT_TRUE(f.delayMs() <= RemoteFrames::kMaxDelayMs);
}

static void test_a_jump_of_the_host_clock_starts_over() {
  RemoteFrames f;
  f.begin(32, 8);
  f.want("Game", "");
  send(f, 0, 100);
  send(f, 24, 124);
  TEST_ASSERT_EQUAL_HEX32(24, shown(f, 400));
  send(f, 30000, 420);
  TEST_ASSERT_EQUAL_HEX32_MESSAGE(24, shown(f, 420), "the old picture stays until the new one is due");
  TEST_ASSERT_EQUAL_HEX32(30000, shown(f, 420 + f.delayMs()));
}

static void test_wide_panels_keep_fewer_frames_and_a_shorter_delay() {
  RemoteFrames f;
  f.begin(128, 8);
  f.want("Game", "");
  Canvas src(128, 8);
  for (int i = 0; i < 20; ++i) {
    src.clear(static_cast<uint32_t>(i));
    for (int row = 0; row < 8; row += 3)
      f.receive(part(src, "Game", static_cast<uint16_t>(i * 24), row, row + 3 > 8 ? 8 - row : 3), 100 + i * 24 + (i % 2) * 90);
  }
  TEST_ASSERT_TRUE(f.delayMs() <= kFramePeriodMs);
}

int main(int, char**) {
  UNITY_BEGIN();
  RUN_TEST(test_a_complete_frame_is_drawn);
  RUN_TEST(test_nothing_is_drawn_before_the_first_frame);
  RUN_TEST(test_a_split_frame_shows_only_once_every_row_is_in);
  RUN_TEST(test_a_new_stamp_drops_a_half_frame);
  RUN_TEST(test_frames_of_apps_not_wanted_are_ignored);
  RUN_TEST(test_both_wanted_apps_keep_their_own_picture);
  RUN_TEST(test_wanting_other_apps_frees_the_old_pictures);
  RUN_TEST(test_a_frame_of_another_size_is_ignored);
  RUN_TEST(test_a_wide_frame_in_three_parts_shows_whole);
  RUN_TEST(test_a_frame_waits_for_its_time);
  RUN_TEST(test_a_burst_is_played_out_frame_by_frame);
  RUN_TEST(test_a_late_frame_never_steps_back);
  RUN_TEST(test_the_delay_has_a_ceiling);
  RUN_TEST(test_a_jump_of_the_host_clock_starts_over);
  RUN_TEST(test_wide_panels_keep_fewer_frames_and_a_shorter_delay);
  return UNITY_END();
}
