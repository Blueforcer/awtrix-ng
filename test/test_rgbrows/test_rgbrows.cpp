#include <unity.h>

#include <cstring>

#include "core/net/Datagram.h"
#include "core/net/RgbRows.h"
#include "core/render/Canvas.h"

using namespace awtrix;
namespace rgb = awtrix::net::rgb;

void setUp() {}
void tearDown() {}

static void test_rows_per_datagram_counts_whole_rows_next_to_the_header() {
  TEST_ASSERT_EQUAL_INT(14, rgb::rowsPerDatagram(32, 0));
  TEST_ASSERT_EQUAL_INT(1, rgb::rowsPerDatagram(32, net::kMaxDatagram - 96));
  TEST_ASSERT_EQUAL_INT(0, rgb::rowsPerDatagram(32, net::kMaxDatagram - 95));
  TEST_ASSERT_EQUAL_INT(0, rgb::rowsPerDatagram(32, net::kMaxDatagram));
  TEST_ASSERT_EQUAL_INT(0, rgb::rowsPerDatagram(32, net::kMaxDatagram + 1));
  TEST_ASSERT_EQUAL_INT(0, rgb::rowsPerDatagram(0, 0));
  TEST_ASSERT_EQUAL_INT(0, rgb::rowsPerDatagram(-1, 0));
}

static void test_valid_rows_takes_only_a_band_inside_the_frame_with_its_exact_size() {
  TEST_ASSERT_TRUE(rgb::validRows(32, 8, 0, 8, 32 * 8 * 3));
  TEST_ASSERT_TRUE(rgb::validRows(32, 8, 6, 2, 32 * 2 * 3));
  TEST_ASSERT_TRUE(rgb::validRows(32, 8, 7, 1, 32 * 3));
  TEST_ASSERT_FALSE(rgb::validRows(32, 8, 6, 2, 32 * 2 * 3 - 1));
  TEST_ASSERT_FALSE(rgb::validRows(32, 8, 6, 2, 32 * 2 * 3 + 1));
  TEST_ASSERT_FALSE(rgb::validRows(32, 8, 6, 3, 32 * 3 * 3));
  TEST_ASSERT_FALSE(rgb::validRows(32, 8, 8, 1, 32 * 3));
  TEST_ASSERT_FALSE(rgb::validRows(32, 8, 0, 0, 0));
  TEST_ASSERT_FALSE(rgb::validRows(0, 8, 0, 8, 0));
  TEST_ASSERT_FALSE(rgb::validRows(32, 0, 0, 0, 0));
  TEST_ASSERT_FALSE(rgb::validRows(32, 8, 1, 0xFFFF, 32 * 3));
}

static void test_pack_writes_the_rows_asked_for_as_red_green_blue() {
  Canvas canvas(2, 3);
  canvas.clear(0x000000);
  canvas.setPixel(0, 1, 0x112233);
  canvas.setPixel(1, 1, 0xA1B2C3);
  canvas.setPixel(0, 2, 0xFFFFFF);
  uint8_t out[16];
  std::memset(out, 0xEE, sizeof(out));
  uint8_t* end = rgb::pack(canvas, 1, 1, out);
  TEST_ASSERT_EQUAL_PTR(out + 6, end);
  const uint8_t expected[] = {0x11, 0x22, 0x33, 0xA1, 0xB2, 0xC3, 0xEE};
  TEST_ASSERT_EQUAL_HEX8_ARRAY(expected, out, sizeof(expected));
}

static void test_draw_puts_a_packed_picture_back() {
  Canvas source(4, 2);
  for (int y = 0; y < 2; ++y)
    for (int x = 0; x < 4; ++x) source.setPixel(x, y, static_cast<uint32_t>(0x010203 * (y * 4 + x + 1)));
  uint8_t rgbBytes[4 * 2 * 3];
  rgb::pack(source, 0, 2, rgbBytes);
  Canvas copy(4, 2);
  copy.clear(0x000000);
  rgb::draw(rgbBytes, copy);
  for (int y = 0; y < 2; ++y)
    for (int x = 0; x < 4; ++x) TEST_ASSERT_EQUAL_HEX32(source.getPixel(x, y), copy.getPixel(x, y));
}

static void test_draw_keeps_to_the_clip() {
  uint8_t white[4 * 2 * 3];
  std::memset(white, 0xFF, sizeof(white));
  Canvas canvas(4, 2);
  canvas.clear(0x000000);
  {
    ClipScope clip(canvas, 0, 0, 2, 2);
    rgb::draw(white, canvas);
  }
  TEST_ASSERT_EQUAL_HEX32(0xFFFFFF, canvas.getPixel(1, 1));
  TEST_ASSERT_EQUAL_HEX32(0x000000, canvas.getPixel(2, 0));
}

int main(int, char**) {
  UNITY_BEGIN();
  RUN_TEST(test_rows_per_datagram_counts_whole_rows_next_to_the_header);
  RUN_TEST(test_valid_rows_takes_only_a_band_inside_the_frame_with_its_exact_size);
  RUN_TEST(test_pack_writes_the_rows_asked_for_as_red_green_blue);
  RUN_TEST(test_draw_puts_a_packed_picture_back);
  RUN_TEST(test_draw_keeps_to_the_clip);
  return UNITY_END();
}
