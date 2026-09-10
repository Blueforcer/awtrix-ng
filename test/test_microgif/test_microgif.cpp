
#include <unity.h>

#include <cstring>
#include <vector>

#include "core/render/Canvas.h"

#include "../../src/media/MicroGif.cpp"

#include "../test_gifplayer/gif_fixtures.h"

void setUp() {}
void tearDown() {}

using awtrix::Canvas;
using awtrix::media::MicroGif;

namespace {

// Explicit code widths make the 9-to-10-bit boundary independent of the decoder under test.
struct CodeBits {
  std::vector<uint8_t> bytes;
  unsigned bit = 0;

  void put(unsigned code, unsigned width = 9) {
    for (unsigned i = 0; i < width; ++i, ++bit) {
      if (bit % 8 == 0) bytes.push_back(0);
      bytes.back() |= ((code >> i) & 1u) << (bit % 8);
    }
  }
};

uint32_t paletteColor(unsigned i) {
  return (i << 16) | ((255 - i) << 8) | ((i * 37 + 13) & 255);
}

std::vector<uint8_t> fullPaletteGif(const CodeBits& codes, bool local = false) {
  std::vector<uint8_t> gif = {
      'G', 'I', 'F', '8', '9', 'a', 32, 0, 8, 0,
      static_cast<uint8_t>(local ? 0 : 0xF7), 0, 0};
  const auto addPalette = [&] {
    for (unsigned i = 0; i < 256; ++i) {
      const uint32_t rgb = paletteColor(i);
      gif.push_back(static_cast<uint8_t>(rgb >> 16));
      gif.push_back(static_cast<uint8_t>(rgb >> 8));
      gif.push_back(static_cast<uint8_t>(rgb));
    }
  };
  if (!local) addPalette();
  const uint8_t image[] = {0x2C, 0, 0, 0, 0, 32, 0, 8, 0,
                           static_cast<uint8_t>(local ? 0x87 : 0)};
  gif.insert(gif.end(), image, image + sizeof(image));
  if (local) addPalette();
  gif.push_back(8);  // Minimum LZW code size: 256 palette entries.
  for (std::size_t at = 0; at < codes.bytes.size();) {
    const std::size_t left = codes.bytes.size() - at;
    const std::size_t n = left < 255 ? left : 255;
    gif.push_back(static_cast<uint8_t>(n));
    gif.insert(gif.end(), codes.bytes.begin() + at, codes.bytes.begin() + at + n);
    at += n;
  }
  gif.push_back(0);
  gif.push_back(0x3B);
  return gif;
}

}

void test_full_256_color_frame_crosses_dictionary_width_boundary() {
  CodeBits bits;
  bits.put(256);  // Clear.
  for (unsigned i = 0; i < 255; ++i) bits.put(i);
  bits.put(255, 10);  // The dictionary reached entry 512 after pixel 254.
  bits.put(257, 10);  // End.
  MicroGif g;
  for (bool local : {false, true}) {
    const auto data = fullPaletteGif(bits, local);
    TEST_ASSERT_TRUE(g.begin(data.data(), data.size()));
    Canvas c(32, 8);
    int delayMs = 0;
    TEST_ASSERT_TRUE(g.nextFrame(c, delayMs) == MicroGif::Step::kFrame);
    for (unsigned i = 0; i < 256; ++i)
      TEST_ASSERT_EQUAL_HEX32(paletteColor(i), c.getPixel(i % 32, i / 32));
    TEST_ASSERT_TRUE(g.nextFrame(c, delayMs) == MicroGif::Step::kEnd);
  }
}

void test_full_frame_kwkwk_at_last_9_bit_dictionary_entry() {
  CodeBits bits;
  bits.put(256);
  for (unsigned i = 0; i < 254; ++i) bits.put(i);
  bits.put(511);  // Next undefined code expands previous pixel 253 twice.
  bits.put(257, 10);
  const auto data = fullPaletteGif(bits);
  MicroGif g;
  TEST_ASSERT_TRUE(g.begin(data.data(), data.size()));
  Canvas c(32, 8);
  int delayMs = 0;
  TEST_ASSERT_TRUE(g.nextFrame(c, delayMs) == MicroGif::Step::kFrame);
  for (unsigned i = 0; i < 256; ++i)
    TEST_ASSERT_EQUAL_HEX32(paletteColor(i < 254 ? i : 253), c.getPixel(i % 32, i / 32));
}

void test_clear_codes_reset_dictionary_through_full_frame() {
  CodeBits bits;
  for (unsigned i = 0; i < 256; ++i) {
    if (i % 32 == 0) bits.put(256);
    bits.put(i);
  }
  bits.put(257);
  const auto data = fullPaletteGif(bits);
  MicroGif g;
  TEST_ASSERT_TRUE(g.begin(data.data(), data.size()));
  Canvas c(32, 8);
  int delayMs = 0;
  TEST_ASSERT_TRUE(g.nextFrame(c, delayMs) == MicroGif::Step::kFrame);
  for (unsigned i = 0; i < 256; ++i)
    TEST_ASSERT_EQUAL_HEX32(paletteColor(i), c.getPixel(i % 32, i / 32));
}

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

  // A local table belongs to one frame; rewinding must recover the original global table.
  g.rewind();
  TEST_ASSERT_TRUE(g.nextFrame(c, delayMs) == MicroGif::Step::kFrame);
  TEST_ASSERT_EQUAL_HEX32(kOddGlobal1, c.getPixel(0, 0));
  TEST_ASSERT_EQUAL_HEX32(kOddGlobal0, c.getPixel(7, 7));
}

void test_failed_reopen_drops_previous_frame_and_palette() {
  MicroGif g;
  TEST_ASSERT_TRUE(g.begin(kGifOddPalette, kGifOddPalette_len));
  TEST_ASSERT_FALSE(g.begin(nullptr, 0));
  g.rewind();
  Canvas c(8, 8);
  int delayMs = 0;
  TEST_ASSERT_TRUE(g.nextFrame(c, delayMs) == MicroGif::Step::kError);
  TEST_ASSERT_TRUE(g.begin(kGif8x8TwoFrames, kGif8x8TwoFrames_len));
  TEST_ASSERT_TRUE(g.nextFrame(c, delayMs) == MicroGif::Step::kFrame);
  TEST_ASSERT_EQUAL_HEX32(0xFF0000u, c.getPixel(0, 0));
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

// Byte-for-byte what the web UI's gifEncode() writes when it converts an uploaded
// PNG/JPG icon (LaMetric 4103: one saturated red on black). The converter is only
// worth having if the firmware decodes its output pixel-exactly.
void test_webui_converted_icon_decodes_exactly() {
  static const unsigned char kIcon[] = {
      0x47, 0x49, 0x46, 0x38, 0x39, 0x61, 0x08, 0x00, 0x08, 0x00, 0x70, 0x00, 0x00, 0x21, 0xff, 0x0b,
      0x4e, 0x45, 0x54, 0x53, 0x43, 0x41, 0x50, 0x45, 0x32, 0x2e, 0x30, 0x03, 0x01, 0x00, 0x00, 0x00,
      0x21, 0xf9, 0x04, 0x04, 0x0a, 0x00, 0x00, 0x00, 0x2c, 0x00, 0x00, 0x00, 0x00, 0x08, 0x00, 0x08,
      0x00, 0x80, 0x00, 0x00, 0x00, 0xf5, 0x00, 0x17, 0x02, 0x0d, 0x84, 0x8f, 0x10, 0x91, 0xbb, 0xe7,
      0x1c, 0x7a, 0x8d, 0xd5, 0xa4, 0x2c, 0x28, 0x00, 0x3b,
  };
  static const char* kRows[8] = {
      "........", "...##...", "..####..", ".######.",
      "...##...", "...##...", "...##...", "...##...",
  };

  MicroGif g;
  TEST_ASSERT_TRUE(g.begin(kIcon, sizeof(kIcon)));
  TEST_ASSERT_EQUAL_INT(8, g.width());
  TEST_ASSERT_EQUAL_INT(8, g.height());

  Canvas c(8, 8);
  c.clear(0x000000u);
  int delayMs = 0;
  TEST_ASSERT_TRUE(g.nextFrame(c, delayMs) == MicroGif::Step::kFrame);
  for (int y = 0; y < 8; ++y)
    for (int x = 0; x < 8; ++x)
      TEST_ASSERT_EQUAL_HEX32(kRows[y][x] == '#' ? 0xF50017u : 0x000000u, c.getPixel(x, y));

  TEST_ASSERT_TRUE(g.nextFrame(c, delayMs) == MicroGif::Step::kEnd);
}

int main(int, char**) {
  UNITY_BEGIN();
  RUN_TEST(test_full_256_color_frame_crosses_dictionary_width_boundary);
  RUN_TEST(test_full_frame_kwkwk_at_last_9_bit_dictionary_entry);
  RUN_TEST(test_clear_codes_reset_dictionary_through_full_frame);
  RUN_TEST(test_failed_reopen_drops_previous_frame_and_palette);
  RUN_TEST(test_webui_converted_icon_decodes_exactly);
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
