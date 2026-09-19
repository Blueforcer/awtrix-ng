#include <unity.h>

#include "core/render/Canvas.h"

using namespace awtrix;

void setUp() {}
void tearDown() {}

static void test_dimensions_and_clear() {
  Canvas c(32, 8);
  TEST_ASSERT_EQUAL_INT(32, c.width());
  TEST_ASSERT_EQUAL_INT(8, c.height());
  TEST_ASSERT_EQUAL_UINT(256u, (unsigned)c.size());
  c.clear(0x010203u);
  TEST_ASSERT_EQUAL_HEX32(0x010203u, c.getPixel(0, 0));
  TEST_ASSERT_EQUAL_HEX32(0x010203u, c.getPixel(31, 7));
}

static void test_setpixel_and_bounds() {
  Canvas c(32, 8);
  c.setPixel(5, 3, 0xFF00FFu);
  TEST_ASSERT_EQUAL_HEX32(0xFF00FFu, c.getPixel(5, 3));
  c.setPixel(-1, 0, 0xFFFFFFu);
  c.setPixel(32, 0, 0xFFFFFFu);
  c.setPixel(0, 8, 0xFFFFFFu);
  TEST_ASSERT_EQUAL_HEX32(0u, c.getPixel(-1, 0));
  TEST_ASSERT_EQUAL_HEX32(0u, c.getPixel(0, 8));
}

static void test_horizontal_line() {
  Canvas c(32, 8);
  c.drawLine(0, 0, 4, 0, 0x00FF00u);
  for (int x = 0; x <= 4; ++x) TEST_ASSERT_EQUAL_HEX32(0x00FF00u, c.getPixel(x, 0));
  TEST_ASSERT_EQUAL_HEX32(0u, c.getPixel(5, 0));
}

static void test_fill_rect() {
  Canvas c(32, 8);
  c.fillRect(2, 2, 3, 2, 0x0000FFu);
  TEST_ASSERT_EQUAL_HEX32(0x0000FFu, c.getPixel(2, 2));
  TEST_ASSERT_EQUAL_HEX32(0x0000FFu, c.getPixel(4, 3));
  TEST_ASSERT_EQUAL_HEX32(0u, c.getPixel(5, 3));
  TEST_ASSERT_EQUAL_HEX32(0u, c.getPixel(2, 4));
}

static void test_draw_rect_outline_only() {
  Canvas c(32, 8);
  c.drawRect(0, 0, 4, 4, 0xFFFFFFu);
  TEST_ASSERT_EQUAL_HEX32(0xFFFFFFu, c.getPixel(0, 0));
  TEST_ASSERT_EQUAL_HEX32(0xFFFFFFu, c.getPixel(3, 3));
  TEST_ASSERT_EQUAL_HEX32(0u, c.getPixel(1, 1));
}

static void test_fill_circle_center_and_radius() {
  Canvas c(32, 8);
  c.fillCircle(4, 4, 2, 0xAABBCCu);
  TEST_ASSERT_EQUAL_HEX32(0xAABBCCu, c.getPixel(4, 4));
  TEST_ASSERT_EQUAL_HEX32(0xAABBCCu, c.getPixel(4, 6));
  TEST_ASSERT_EQUAL_HEX32(0u, c.getPixel(4, 7));
}

static void test_external_buffer_drawing_and_clear() {
  uint32_t pixels[] = {0x010203u, 0u, 0u, 0u, 0u, 0u};
  Canvas c(3, 2, pixels);
  const Canvas& readOnly = c;
  TEST_ASSERT_EQUAL_PTR(pixels, c.data());
  TEST_ASSERT_EQUAL_PTR(pixels, readOnly.data());
  TEST_ASSERT_EQUAL_UINT(6u, (unsigned)c.size());
  TEST_ASSERT_EQUAL_HEX32(0x010203u, c.getPixel(0, 0));
  c.setClipX(1, 1);
  c.fillRect(-1, -1, 5, 4, 0xFF123456u);
  TEST_ASSERT_EQUAL_HEX32(0x010203u, pixels[0]);
  TEST_ASSERT_EQUAL_HEX32(0x123456u, pixels[1]);
  TEST_ASSERT_EQUAL_HEX32(0u, pixels[2]);
  TEST_ASSERT_EQUAL_HEX32(0x123456u, pixels[4]);
  TEST_ASSERT_EQUAL_HEX32(0u, c.getPixel(3, 1));
  c.clear(0xABCDEFu);
  for (uint32_t pixel : pixels) TEST_ASSERT_EQUAL_HEX32(0xABCDEFu, pixel);
}

static void test_empty_external_buffers() {
  uint32_t pixel = 0x123456u;
  Canvas canvases[] = {
      Canvas(3, 2, nullptr), Canvas(0, 2, &pixel), Canvas(3, 0, &pixel),
      Canvas(-1, 2, &pixel), Canvas(3, -1, &pixel)};
  for (auto& c : canvases) {
    TEST_ASSERT_EQUAL_INT(0, c.width());
    TEST_ASSERT_EQUAL_INT(0, c.height());
    TEST_ASSERT_EQUAL_UINT(0u, (unsigned)c.size());
    c.setPixel(0, 0, 0xFFFFFFu);
    c.clear();
    TEST_ASSERT_EQUAL_HEX32(0u, c.getPixel(0, 0));
  }
  TEST_ASSERT_EQUAL_HEX32(0x123456u, pixel);
}

static void test_owning_copy_keeps_independent_pixels() {
  Canvas original(2, 1);
  original.setPixel(0, 0, 0x123456u);
  Canvas copy = original;
  copy.setPixel(0, 0, 0xABCDEFu);
  TEST_ASSERT_EQUAL_HEX32(0x123456u, original.getPixel(0, 0));
  TEST_ASSERT_EQUAL_HEX32(0xABCDEFu, copy.getPixel(0, 0));
}

int main(int, char**) {
  UNITY_BEGIN();
  RUN_TEST(test_dimensions_and_clear);
  RUN_TEST(test_setpixel_and_bounds);
  RUN_TEST(test_horizontal_line);
  RUN_TEST(test_fill_rect);
  RUN_TEST(test_draw_rect_outline_only);
  RUN_TEST(test_fill_circle_center_and_radius);
  RUN_TEST(test_external_buffer_drawing_and_clear);
  RUN_TEST(test_empty_external_buffers);
  RUN_TEST(test_owning_copy_keeps_independent_pixels);
  return UNITY_END();
}
