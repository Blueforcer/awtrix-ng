
#include <unity.h>

#include <cstring>

#include "core/render/Canvas.h"

#include "../../src/media/MicroGif.cpp"

#include "../test_gifplayer/gif_fixtures.h"

void setUp() {}
void tearDown() {}

using awtrix::Canvas;
using awtrix::media::MicroGif;

void test_two_frames_delays_end_and_rewind() {
  MicroGif g;
  TEST_ASSERT_TRUE(g.begin(kGif8x8TwoFrames, kGif8x8TwoFrames_len));
  TEST_ASSERT_EQUAL_INT(8, g.width());
  TEST_ASSERT_EQUAL_INT(8, g.height());

  Canvas c(8, 8);
  c.clear(0x000000u);
  int delayMs = 0;

  TEST_ASSERT_TRUE(g.nextFrame(c, delayMs) == MicroGif::Step::kFrame);
  TEST_ASSERT_EQUAL_INT(200, delayMs);
  TEST_ASSERT_EQUAL_HEX32(0xFF0000u, c.getPixel(0, 0));
  TEST_ASSERT_EQUAL_HEX32(0xFF0000u, c.getPixel(7, 7));

  TEST_ASSERT_TRUE(g.nextFrame(c, delayMs) == MicroGif::Step::kFrame);
  TEST_ASSERT_EQUAL_INT(300, delayMs);
  TEST_ASSERT_EQUAL_HEX32(0x00FF00u, c.getPixel(0, 0));

  TEST_ASSERT_TRUE(g.nextFrame(c, delayMs) == MicroGif::Step::kEnd);

  g.rewind();
  c.clear(0x000000u);
  TEST_ASSERT_TRUE(g.nextFrame(c, delayMs) == MicroGif::Step::kFrame);
  TEST_ASSERT_EQUAL_HEX32(0xFF0000u, c.getPixel(3, 4));
}

void test_32x8_full_width() {
  MicroGif g;
  TEST_ASSERT_TRUE(g.begin(kGif32x8TwoFrames, kGif32x8TwoFrames_len));
  TEST_ASSERT_EQUAL_INT(32, g.width());
  TEST_ASSERT_EQUAL_INT(8, g.height());

  Canvas c(32, 8);
  c.clear(0x000000u);
  int delayMs = 0;
  TEST_ASSERT_TRUE(g.nextFrame(c, delayMs) == MicroGif::Step::kFrame);
  TEST_ASSERT_EQUAL_HEX32(0x0000FFu, c.getPixel(0, 0));
  TEST_ASSERT_EQUAL_HEX32(0x0000FFu, c.getPixel(31, 7));
}

void test_transparent_pixels_leave_canvas_untouched() {
  MicroGif g;
  TEST_ASSERT_TRUE(g.begin(kGifTransparentStatic, kGifTransparentStatic_len));

  Canvas c(8, 8);
  c.clear(0xFFFFFFu);
  int delayMs = 0;
  TEST_ASSERT_TRUE(g.nextFrame(c, delayMs) == MicroGif::Step::kFrame);
  TEST_ASSERT_EQUAL_HEX32(0xFF0000u, c.getPixel(0, 0));
  TEST_ASSERT_EQUAL_HEX32(0xFF0000u, c.getPixel(3, 7));
  TEST_ASSERT_EQUAL_HEX32(0xFFFFFFu, c.getPixel(4, 0));
  TEST_ASSERT_EQUAL_HEX32(0xFFFFFFu, c.getPixel(7, 7));
}

void test_transparent_animation_composites_over_previous_frame() {
  MicroGif g;
  TEST_ASSERT_TRUE(g.begin(kGifTransparentAnim, kGifTransparentAnim_len));

  Canvas c(8, 8);
  c.clear(0x000000u);
  int delayMs = 0;
  TEST_ASSERT_TRUE(g.nextFrame(c, delayMs) == MicroGif::Step::kFrame);
  TEST_ASSERT_EQUAL_HEX32(0x00FF00u, c.getPixel(0, 0));
  TEST_ASSERT_EQUAL_HEX32(0x00FF00u, c.getPixel(7, 7));

  TEST_ASSERT_TRUE(g.nextFrame(c, delayMs) == MicroGif::Step::kFrame);
  TEST_ASSERT_EQUAL_HEX32(0x0000FFu, c.getPixel(0, 0));
  TEST_ASSERT_EQUAL_HEX32(0x0000FFu, c.getPixel(3, 3));
  TEST_ASSERT_EQUAL_HEX32(0x00FF00u, c.getPixel(7, 7));
}

void test_all_33_streaming_frames_decode_then_end() {
  MicroGif g;
  TEST_ASSERT_TRUE(g.begin(kGif32x8ManyFrames, kGif32x8ManyFrames_len));

  Canvas c(32, 8);
  c.clear(0x000000u);
  int delayMs = 0;
  for (int i = 0; i < 33; ++i) {
    TEST_ASSERT_TRUE(g.nextFrame(c, delayMs) == MicroGif::Step::kFrame);
    TEST_ASSERT_EQUAL_HEX32(i % 2 == 0 ? 0xFF0000u : 0x0000FFu, c.getPixel(16, 4));
  }
  TEST_ASSERT_TRUE(g.nextFrame(c, delayMs) == MicroGif::Step::kEnd);

  g.rewind();
  c.clear(0x000000u);
  TEST_ASSERT_TRUE(g.nextFrame(c, delayMs) == MicroGif::Step::kFrame);
  TEST_ASSERT_EQUAL_HEX32(0xFF0000u, c.getPixel(16, 4));
}

void test_odd_palette_decodes_at_full_depth() {
  MicroGif g;
  TEST_ASSERT_TRUE(g.begin(kGifOddPalette, kGifOddPalette_len));

  Canvas c(8, 8);
  c.clear(0x000000u);
  int delayMs = 0;

  TEST_ASSERT_TRUE(g.nextFrame(c, delayMs) == MicroGif::Step::kFrame);
  TEST_ASSERT_EQUAL_INT(200, delayMs);
  TEST_ASSERT_EQUAL_HEX32(kOddGlobal1, c.getPixel(0, 0));
  TEST_ASSERT_EQUAL_HEX32(kOddGlobal1, c.getPixel(3, 7));
  TEST_ASSERT_EQUAL_HEX32(kOddGlobal0, c.getPixel(4, 0));
  TEST_ASSERT_EQUAL_HEX32(kOddGlobal0, c.getPixel(7, 7));

  TEST_ASSERT_TRUE(g.nextFrame(c, delayMs) == MicroGif::Step::kFrame);
  TEST_ASSERT_EQUAL_INT(300, delayMs);
  TEST_ASSERT_EQUAL_HEX32(kOddLocal1, c.getPixel(0, 0));
  TEST_ASSERT_EQUAL_HEX32(kOddLocal0, c.getPixel(7, 7));
}

void test_not_a_gif_rejected() {
  static const unsigned char junk[] = "JFIF definitely not a gif, long enough";
  MicroGif g;
  TEST_ASSERT_FALSE(g.begin(junk, sizeof(junk)));
  TEST_ASSERT_EQUAL_INT(0, g.width());
}

void test_truncated_stream_survives() {
  for (unsigned int truncLen = 0; truncLen < kGif8x8TwoFrames_len; ++truncLen) {
    MicroGif g;
    if (!g.begin(kGif8x8TwoFrames, truncLen)) continue;
    Canvas c(8, 8);
    int delayMs = 0;
    for (int i = 0; i < 4; ++i) {
      const MicroGif::Step st = g.nextFrame(c, delayMs);
      if (st != MicroGif::Step::kFrame) break;
    }
  }
  TEST_PASS();
}

void test_oversize_frame_rejected() {
  static const unsigned char oversize[] = {
      'G', 'I', 'F', '8', '9', 'a',
      64, 0, 16, 0,
      0x00, 0x00, 0x00,
      0x2C,
      0, 0, 0, 0,
      64, 0, 16, 0,
      0x00,
      0x02, 0x00,
      0x3B,
  };
  MicroGif g;
  TEST_ASSERT_TRUE(g.begin(oversize, sizeof(oversize)));
  TEST_ASSERT_EQUAL_INT(32, g.width());
  TEST_ASSERT_EQUAL_INT(8, g.height());

  Canvas c(32, 8);
  int delayMs = 0;
  TEST_ASSERT_TRUE(g.nextFrame(c, delayMs) == MicroGif::Step::kError);
}

int main(int, char**) {
  UNITY_BEGIN();
  RUN_TEST(test_two_frames_delays_end_and_rewind);
  RUN_TEST(test_32x8_full_width);
  RUN_TEST(test_transparent_pixels_leave_canvas_untouched);
  RUN_TEST(test_transparent_animation_composites_over_previous_frame);
  RUN_TEST(test_all_33_streaming_frames_decode_then_end);
  RUN_TEST(test_odd_palette_decodes_at_full_depth);
  RUN_TEST(test_not_a_gif_rejected);
  RUN_TEST(test_truncated_stream_survives);
  RUN_TEST(test_oversize_frame_rejected);
  return UNITY_END();
}
