
#include <unity.h>

#include <array>
#include <cstdlib>
#include <cstring>
#include <limits>
#include <new>
#include <utility>
#include <vector>

#include "core/render/Canvas.h"

namespace {
bool s_trackMediaAllocs = false;
std::size_t s_mediaBytes = 0;
unsigned s_mediaAllocs = 0;
unsigned s_failMediaAlloc = 0;
std::size_t s_mediaLiveBytes = 0;
struct MediaAllocation { void* pointer = nullptr; std::size_t bytes = 0; };
MediaAllocation s_mediaAllocations[64];

void releaseAllocation(void* p) {
  for (auto& allocation : s_mediaAllocations) {
    if (p && allocation.pointer == p) {
      s_mediaLiveBytes -= allocation.bytes;
      allocation.pointer = nullptr;
      break;
    }
  }
  std::free(p);
}

void startMediaAllocProbe(unsigned failAllocation = 0) {
  s_mediaBytes = 0;
  s_mediaAllocs = 0;
  s_failMediaAlloc = failAllocation;
  s_trackMediaAllocs = true;
}
}

// MediaHeap uses nothrow new[]. Restrict probes to that path so fixture construction and
// Canvas storage do not count towards the GIF decoder's working memory.
void* operator new(std::size_t n) {
  if (void* p = std::malloc(n)) return p;
  throw std::bad_alloc();
}
void* operator new[](std::size_t n) { return operator new(n); }
void* operator new(std::size_t n, const std::nothrow_t&) noexcept { return std::malloc(n); }
void* operator new[](std::size_t n, const std::nothrow_t&) noexcept {
  if (s_trackMediaAllocs) {
    s_mediaBytes += n;
    if (++s_mediaAllocs == s_failMediaAlloc) return nullptr;
  }
  void* p = std::malloc(n);
  if (p) {
    for (auto& allocation : s_mediaAllocations) {
      if (!allocation.pointer) {
        allocation = {p, n};
        s_mediaLiveBytes += n;
        return p;
      }
    }
    std::abort();  // A full probe table is a test-instrumentation error.
  }
  return p;
}
void operator delete(void* p) noexcept { releaseAllocation(p); }
void operator delete(void* p, std::size_t) noexcept { releaseAllocation(p); }
void operator delete(void* p, const std::nothrow_t&) noexcept { releaseAllocation(p); }
void operator delete[](void* p) noexcept { releaseAllocation(p); }
void operator delete[](void* p, std::size_t) noexcept { releaseAllocation(p); }
void operator delete[](void* p, const std::nothrow_t&) noexcept { releaseAllocation(p); }

#include "../../src/media/MicroGif.cpp"

#include "../test_gifplayer/gif_fixtures.h"

void setUp() { s_trackMediaAllocs = false; }
void tearDown() { s_trackMediaAllocs = false; }

using awtrix::Canvas;
using awtrix::media::MicroGif;

namespace {

// Three 4x4 frames: a red base, a transparent blue delta using GIF disposal 3
// (restore to previous), then a transparent green delta. The third composed frame must recover
// the red base before drawing green; clearing the blue frame's rectangle loses that base.
static const unsigned char kGifRestorePrevious[] = {
    0x47, 0x49, 0x46, 0x38, 0x39, 0x61, 0x04, 0x00, 0x04, 0x00, 0x81, 0x00, 0x00, 0xff, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x21, 0xff, 0x0b, 0x4e, 0x45, 0x54, 0x53,
    0x43, 0x41, 0x50, 0x45, 0x32, 0x2e, 0x30, 0x03, 0x01, 0x00, 0x00, 0x00, 0x21, 0xf9, 0x04, 0x04,
    0x0a, 0x00, 0x00, 0x00, 0x2c, 0x00, 0x00, 0x00, 0x00, 0x04, 0x00, 0x04, 0x00, 0x00, 0x08, 0x09,
    0x00, 0x01, 0x08, 0x1c, 0x48, 0xb0, 0x20, 0x80, 0x80, 0x00, 0x21, 0xf9, 0x04, 0x0d, 0x0a, 0x00,
    0x00, 0x00, 0x2c, 0x00, 0x00, 0x00, 0x00, 0x04, 0x00, 0x04, 0x00, 0x81, 0x00, 0x00, 0x00, 0x00,
    0x00, 0xff, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x08, 0x09, 0x00, 0x03, 0x00, 0x18, 0x48, 0xb0,
    0xa0, 0xc1, 0x80, 0x00, 0x21, 0xf9, 0x04, 0x05, 0x0a, 0x00, 0x00, 0x00, 0x2c, 0x00, 0x00, 0x00,
    0x00, 0x04, 0x00, 0x04, 0x00, 0x81, 0x00, 0x00, 0x00, 0x00, 0xff, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x08, 0x09, 0x00, 0x01, 0x08, 0x1c, 0x48, 0xb0, 0x60, 0x80, 0x80, 0x00, 0x3b,
};

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

std::vector<uint8_t> fullPaletteGif(const CodeBits& codes, bool local = false,
                                    unsigned width = 32, unsigned height = 8,
                                    bool interlaced = false) {
  std::vector<uint8_t> gif = {
      'G', 'I', 'F', '8', '9', 'a',
      static_cast<uint8_t>(width), static_cast<uint8_t>(width >> 8),
      static_cast<uint8_t>(height), static_cast<uint8_t>(height >> 8),
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
  const uint8_t image[] = {
      0x2C, 0, 0, 0, 0,
      static_cast<uint8_t>(width), static_cast<uint8_t>(width >> 8),
      static_cast<uint8_t>(height), static_cast<uint8_t>(height >> 8),
      static_cast<uint8_t>((local ? 0x87 : 0) | (interlaced ? 0x40 : 0))};
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

CodeBits literalCodes(const std::vector<uint8_t>& pixels) {
  CodeBits bits;
  bits.put(256);
  for (std::size_t i = 0; i < pixels.size(); ++i) {
    // GIF's 256-entry initial table crosses these exact pixel boundaries, then
    // remains at 12 bits even when the 4096-entry dictionary is full.
    const unsigned width = i < 255 ? 9 : i < 767 ? 10 : i < 1791 ? 11 : 12;
    bits.put(pixels[i], width);
  }
  const std::size_t n = pixels.size();
  bits.put(257, n < 255 ? 9 : n < 767 ? 10 : n < 1791 ? 11 : 12);
  return bits;
}

void appendFrame(std::vector<uint8_t>& gif, unsigned width, unsigned height,
                 const std::vector<uint8_t>& pixels, unsigned disposal = 1,
                 bool transparent = false, unsigned x = 0, unsigned y = 0) {
  gif.pop_back();  // Replace the preceding trailer with another frame.
  const uint8_t gce[] = {0x21, 0xF9, 4,
                         static_cast<uint8_t>((disposal << 2) | (transparent ? 1 : 0)),
                         1, 0, 0, 0};
  gif.insert(gif.end(), gce, gce + sizeof(gce));
  auto frame = fullPaletteGif(literalCodes(pixels), false, width, height);
  constexpr std::size_t kImageOffset = 13 + 256 * 3;
  frame[kImageOffset + 1] = static_cast<uint8_t>(x);
  frame[kImageOffset + 2] = static_cast<uint8_t>(x >> 8);
  frame[kImageOffset + 3] = static_cast<uint8_t>(y);
  frame[kImageOffset + 4] = static_cast<uint8_t>(y >> 8);
  gif.insert(gif.end(), frame.begin() + kImageOffset, frame.end());
}

}

void test_restore_to_previous_recovers_pixels_under_transparent_delta() {
  MicroGif g;
  TEST_ASSERT_TRUE(g.begin(kGifRestorePrevious, sizeof(kGifRestorePrevious), 32, 8));
  Canvas c(4, 4);
  c.clear(0x000000u);
  int delayMs = 0;

  TEST_ASSERT_TRUE(g.nextFrame(c, delayMs) == MicroGif::Step::kFrame);
  TEST_ASSERT_EQUAL_HEX32(0xFF0000u, c.getPixel(0, 0));
  TEST_ASSERT_EQUAL_HEX32(0xFF0000u, c.getPixel(3, 3));

  TEST_ASSERT_TRUE(g.nextFrame(c, delayMs) == MicroGif::Step::kFrame);
  TEST_ASSERT_EQUAL_HEX32(0x0000FFu, c.getPixel(0, 0));
  TEST_ASSERT_EQUAL_HEX32(0xFF0000u, c.getPixel(1, 0));

  TEST_ASSERT_TRUE(g.nextFrame(c, delayMs) == MicroGif::Step::kFrame);
  TEST_ASSERT_EQUAL_HEX32(0xFF0000u, c.getPixel(0, 0));
  TEST_ASSERT_EQUAL_HEX32(0xFF0000u, c.getPixel(1, 0));
  TEST_ASSERT_EQUAL_HEX32(0x00FF00u, c.getPixel(3, 3));
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
    TEST_ASSERT_TRUE(g.begin(data.data(), data.size(), 32, 8));
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
  TEST_ASSERT_TRUE(g.begin(data.data(), data.size(), 32, 8));
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
  TEST_ASSERT_TRUE(g.begin(data.data(), data.size(), 32, 8));
  Canvas c(32, 8);
  int delayMs = 0;
  TEST_ASSERT_TRUE(g.nextFrame(c, delayMs) == MicroGif::Step::kFrame);
  for (unsigned i = 0; i < 256; ++i)
    TEST_ASSERT_EQUAL_HEX32(paletteColor(i), c.getPixel(i % 32, i / 32));
}

void test_two_frames_delays_end_and_rewind() {
  MicroGif g;
  TEST_ASSERT_TRUE(g.begin(kGif8x8TwoFrames, kGif8x8TwoFrames_len, 32, 8));
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
  TEST_ASSERT_TRUE(g.begin(kGif32x8TwoFrames, kGif32x8TwoFrames_len, 32, 8));
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
  TEST_ASSERT_TRUE(g.begin(kGifTransparentStatic, kGifTransparentStatic_len, 32, 8));

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
  TEST_ASSERT_TRUE(g.begin(kGifTransparentAnim, kGifTransparentAnim_len, 32, 8));

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
  TEST_ASSERT_TRUE(g.begin(kGif32x8ManyFrames, kGif32x8ManyFrames_len, 32, 8));

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
  TEST_ASSERT_TRUE(g.begin(kGifOddPalette, kGifOddPalette_len, 32, 8));

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
  TEST_ASSERT_TRUE(g.begin(kGifOddPalette, kGifOddPalette_len, 32, 8));
  TEST_ASSERT_FALSE(g.begin(nullptr, 0, 32, 8));
  g.rewind();
  Canvas c(8, 8);
  int delayMs = 0;
  TEST_ASSERT_TRUE(g.nextFrame(c, delayMs) == MicroGif::Step::kError);
  TEST_ASSERT_TRUE(g.begin(kGif8x8TwoFrames, kGif8x8TwoFrames_len, 32, 8));
  TEST_ASSERT_TRUE(g.nextFrame(c, delayMs) == MicroGif::Step::kFrame);
  TEST_ASSERT_EQUAL_HEX32(0xFF0000u, c.getPixel(0, 0));
}

void test_not_a_gif_rejected() {
  static const unsigned char junk[] = "JFIF definitely not a gif, long enough";
  MicroGif g;
  TEST_ASSERT_FALSE(g.begin(junk, sizeof(junk), 32, 8));
  TEST_ASSERT_EQUAL_INT(0, g.width());
}

void test_truncated_stream_survives() {
  for (unsigned int truncLen = 0; truncLen < kGif8x8TwoFrames_len; ++truncLen) {
    MicroGif g;
    if (!g.begin(kGif8x8TwoFrames, truncLen, 32, 8)) continue;
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
  TEST_ASSERT_TRUE(g.begin(oversize, sizeof(oversize), 32, 8));
  TEST_ASSERT_EQUAL_INT(32, g.width());
  TEST_ASSERT_EQUAL_INT(8, g.height());

  Canvas c(32, 8);
  int delayMs = 0;
  TEST_ASSERT_TRUE(g.nextFrame(c, delayMs) == MicroGif::Step::kError);
}

void test_runtime_panel_dimensions_decode_41x8_and_51x16() {
  for (const auto dimensions : {std::pair<unsigned, unsigned>{41, 8}, {51, 16}}) {
    const unsigned width = dimensions.first;
    const unsigned height = dimensions.second;
    std::vector<uint8_t> pixels(width * height);
    for (unsigned i = 0; i < pixels.size(); ++i) pixels[i] = static_cast<uint8_t>(i);
    const auto data = fullPaletteGif(literalCodes(pixels), false, width, height);
    MicroGif g;
    TEST_ASSERT_TRUE(g.begin(data.data(), data.size(), width, height));
    TEST_ASSERT_EQUAL_INT(width, g.width());
    TEST_ASSERT_EQUAL_INT(height, g.height());
    Canvas c(width, height);
    int delayMs = 0;
    TEST_ASSERT_TRUE(g.nextFrame(c, delayMs) == MicroGif::Step::kFrame);
    for (unsigned i = 0; i < pixels.size(); ++i)
      TEST_ASSERT_EQUAL_HEX32(paletteColor(pixels[i]), c.getPixel(i % width, i / width));
    TEST_ASSERT_TRUE(g.nextFrame(c, delayMs) == MicroGif::Step::kEnd);
  }
}

void test_interlaced_tall_frame_maps_all_four_passes() {
  constexpr unsigned width = 51, height = 17;
  std::vector<uint8_t> pixels;
  const unsigned starts[] = {0, 4, 2, 1};
  const unsigned steps[] = {8, 8, 4, 2};
  for (unsigned pass = 0; pass < 4; ++pass)
    for (unsigned y = starts[pass]; y < height; y += steps[pass])
      for (unsigned x = 0; x < width; ++x)
        pixels.push_back(static_cast<uint8_t>(y * 11 + x));
  const auto data = fullPaletteGif(literalCodes(pixels), false, width, height, true);
  MicroGif g;
  TEST_ASSERT_TRUE(g.begin(data.data(), data.size(), width, height));
  Canvas c(width, height);
  int delayMs = 0;
  TEST_ASSERT_TRUE(g.nextFrame(c, delayMs) == MicroGif::Step::kFrame);
  for (unsigned y = 0; y < height; ++y)
    for (unsigned x = 0; x < width; ++x)
      TEST_ASSERT_EQUAL_HEX32(paletteColor((y * 11 + x) & 255), c.getPixel(x, y));
}

void test_large_restore_previous_and_background_disposal() {
  constexpr unsigned width = 51, height = 16;
  auto data = fullPaletteGif(literalCodes(std::vector<uint8_t>(width * height, 1)),
                            false, width, height);
  std::vector<uint8_t> overlay(width * height, 0);
  overlay.front() = overlay.back() = 2;
  appendFrame(data, width, height, overlay, 3, true);
  appendFrame(data, 1, 1, {3}, 2, false, width - 1, height - 1);
  appendFrame(data, 1, 1, {4});
  MicroGif g;
  TEST_ASSERT_TRUE(g.begin(data.data(), data.size(), width, height));
  Canvas c(width, height);
  int delayMs = 0;
  TEST_ASSERT_TRUE(g.nextFrame(c, delayMs) == MicroGif::Step::kFrame);
  TEST_ASSERT_TRUE(g.nextFrame(c, delayMs) == MicroGif::Step::kFrame);
  TEST_ASSERT_EQUAL_HEX32(paletteColor(2), c.getPixel(0, 0));
  TEST_ASSERT_EQUAL_HEX32(paletteColor(2), c.getPixel(width - 1, height - 1));
  TEST_ASSERT_EQUAL_HEX32(paletteColor(1), c.getPixel(width - 2, height - 1));
  TEST_ASSERT_TRUE(g.nextFrame(c, delayMs) == MicroGif::Step::kFrame);
  TEST_ASSERT_EQUAL_HEX32(paletteColor(1), c.getPixel(0, 0));
  TEST_ASSERT_EQUAL_HEX32(paletteColor(3), c.getPixel(width - 1, height - 1));
  TEST_ASSERT_TRUE(g.nextFrame(c, delayMs) == MicroGif::Step::kFrame);
  TEST_ASSERT_EQUAL_HEX32(paletteColor(4), c.getPixel(0, 0));
  TEST_ASSERT_EQUAL_HEX32(0, c.getPixel(width - 1, height - 1));
  TEST_ASSERT_EQUAL_HEX32(paletteColor(1), c.getPixel(width - 2, height - 1));
}

void test_small_icon_working_memory_is_independent_of_panel_dimensions() {
  std::size_t referenceBytes = 0;
  for (const auto dimensions : {std::pair<int, int>{32, 8}, {51, 16}, {1024, 1024}}) {
    MicroGif g;
    Canvas c(8, 8);
    int delayMs = 0;
    startMediaAllocProbe();
    const bool opened = g.begin(kGif8x8TwoFrames, kGif8x8TwoFrames_len,
                                dimensions.first, dimensions.second);
    const auto step = g.nextFrame(c, delayMs);
    s_trackMediaAllocs = false;
    TEST_ASSERT_TRUE(opened);
    TEST_ASSERT_TRUE(step == MicroGif::Step::kFrame);
    TEST_ASSERT_GREATER_THAN(0, s_mediaAllocs);
    TEST_ASSERT_EQUAL_UINT32(320, s_mediaBytes);
    if (referenceBytes == 0) referenceBytes = s_mediaBytes;
    TEST_ASSERT_EQUAL_UINT32(referenceBytes, s_mediaBytes);
    TEST_ASSERT_EQUAL_INT(8, g.width());
    TEST_ASSERT_EQUAL_INT(8, g.height());
  }
}

void test_decoder_allocation_failure_retries_the_same_frame() {
  const auto data = fullPaletteGif(literalCodes(std::vector<uint8_t>(51 * 16, 7)),
                                   false, 51, 16);
  MicroGif g;
  TEST_ASSERT_TRUE(g.begin(data.data(), data.size(), 51, 16));
  Canvas c(51, 16);
  c.clear(0x123456);
  int delayMs = 99;
  startMediaAllocProbe(1);
  const auto failed = g.nextFrame(c, delayMs, true);
  s_trackMediaAllocs = false;
  TEST_ASSERT_TRUE(failed == MicroGif::Step::kOom);
  TEST_ASSERT_EQUAL_INT(0, delayMs);
  TEST_ASSERT_EQUAL_HEX32(0x123456, c.getPixel(50, 15));
  TEST_ASSERT_TRUE(g.nextFrame(c, delayMs, true) == MicroGif::Step::kFrame);
  TEST_ASSERT_EQUAL_HEX32(paletteColor(7), c.getPixel(50, 15));
}

void test_restore_snapshot_allocation_failure_returns_oom() {
  auto data = fullPaletteGif(literalCodes(std::vector<uint8_t>(51 * 16, 1)),
                            false, 51, 16);
  appendFrame(data, 51, 16, std::vector<uint8_t>(51 * 16, 2), 3);
  MicroGif g;
  TEST_ASSERT_TRUE(g.begin(data.data(), data.size(), 51, 16));
  Canvas c(51, 16);
  int delayMs = 0;
  TEST_ASSERT_TRUE(g.nextFrame(c, delayMs) == MicroGif::Step::kFrame);
  // Scratch is already warm; only restore-to-previous needs a new buffer.
  startMediaAllocProbe(1);
  const auto failed = g.nextFrame(c, delayMs);
  s_trackMediaAllocs = false;
  TEST_ASSERT_TRUE(failed == MicroGif::Step::kOom);
  TEST_ASSERT_EQUAL_INT(0, delayMs);
  TEST_ASSERT_EQUAL_HEX32(paletteColor(1), c.getPixel(0, 0));
  TEST_ASSERT_EQUAL_HEX32(paletteColor(1), c.getPixel(50, 15));
  TEST_ASSERT_TRUE(g.nextFrame(c, delayMs) == MicroGif::Step::kFrame);
  TEST_ASSERT_EQUAL_INT(10, delayMs);
  TEST_ASSERT_EQUAL_HEX32(paletteColor(2), c.getPixel(50, 15));
}

void test_oom_preserves_pending_transparency_delay_and_disposal() {
  auto data = fullPaletteGif(literalCodes(std::vector<uint8_t>(51 * 16, 1)), false, 51, 16);
  std::vector<uint8_t> overlay(51 * 16, 0);
  overlay.front() = 2;
  appendFrame(data, 51, 16, overlay, 3, true);
  appendFrame(data, 1, 1, {3}, 1, false, 50, 15);
  MicroGif g;
  TEST_ASSERT_TRUE(g.begin(data.data(), data.size(), 51, 16));
  Canvas c(51, 16);
  int delayMs = 0;
  TEST_ASSERT_TRUE(g.nextFrame(c, delayMs) == MicroGif::Step::kFrame);
  for (int attempt = 0; attempt < 2; ++attempt) {
    startMediaAllocProbe(1);
    const auto failed = g.nextFrame(c, delayMs);
    s_trackMediaAllocs = false;
    TEST_ASSERT_TRUE(failed == MicroGif::Step::kOom);
    TEST_ASSERT_EQUAL_INT(0, delayMs);
    TEST_ASSERT_EQUAL_HEX32(paletteColor(1), c.getPixel(0, 0));
  }
  TEST_ASSERT_TRUE(g.nextFrame(c, delayMs) == MicroGif::Step::kFrame);
  TEST_ASSERT_EQUAL_INT(10, delayMs);
  TEST_ASSERT_EQUAL_HEX32(paletteColor(2), c.getPixel(0, 0));
  TEST_ASSERT_EQUAL_HEX32(paletteColor(1), c.getPixel(50, 15));
  TEST_ASSERT_TRUE(g.nextFrame(c, delayMs) == MicroGif::Step::kFrame);
  TEST_ASSERT_EQUAL_HEX32(paletteColor(1), c.getPixel(0, 0));
  TEST_ASSERT_EQUAL_HEX32(paletteColor(3), c.getPixel(50, 15));
}

void test_shared_workspace_is_reused_by_five_alternating_decoders() {
  std::array<MicroGif, 5> decoders;
  Canvas canvas(8, 8);
  int delayMs = 0;
  for (auto& decoder : decoders) {
    TEST_ASSERT_TRUE(decoder.begin(kGif8x8TwoFrames, kGif8x8TwoFrames_len, 51, 16));
    TEST_ASSERT_TRUE(decoder.nextFrame(canvas, delayMs) == MicroGif::Step::kFrame);
  }
  startMediaAllocProbe();
  for (int frame = 0; frame < 10; ++frame)
    for (auto& decoder : decoders) {
      if (frame % 2) decoder.rewind();
      TEST_ASSERT_TRUE(decoder.nextFrame(canvas, delayMs) == MicroGif::Step::kFrame);
      TEST_ASSERT_EQUAL_HEX32(frame % 2 ? 0xff0000u : 0x00ff00u, canvas.getPixel(7, 7));
    }
  s_trackMediaAllocs = false;
  TEST_ASSERT_EQUAL_UINT32(0, s_mediaAllocs);
  TEST_ASSERT_EQUAL_UINT32(320, s_mediaLiveBytes);
  for (auto& decoder : decoders) decoder = MicroGif{};
  TEST_ASSERT_EQUAL_UINT32(0, s_mediaLiveBytes);
}

void test_workspace_releases_large_capacity_when_largest_owner_closes() {
  const auto data = fullPaletteGif(literalCodes(std::vector<uint8_t>(51 * 16, 7)), false, 51, 16);
  MicroGif small, large;
  Canvas c(51, 16);
  int delayMs = 0;
  TEST_ASSERT_TRUE(small.begin(kGif8x8TwoFrames, kGif8x8TwoFrames_len, 51, 16));
  TEST_ASSERT_TRUE(small.nextFrame(c, delayMs) == MicroGif::Step::kFrame);
  const auto smallBytes = s_mediaLiveBytes;
  TEST_ASSERT_TRUE(large.begin(data.data(), data.size(), 51, 16));
  TEST_ASSERT_TRUE(large.nextFrame(c, delayMs) == MicroGif::Step::kFrame);
  TEST_ASSERT_GREATER_THAN(smallBytes, s_mediaLiveBytes);
  large = MicroGif{};
  TEST_ASSERT_LESS_OR_EQUAL_UINT32(smallBytes, s_mediaLiveBytes);
  TEST_ASSERT_TRUE(small.nextFrame(c, delayMs) == MicroGif::Step::kFrame);
  TEST_ASSERT_EQUAL_HEX32(0x00ff00, c.getPixel(7, 7));
  TEST_ASSERT_EQUAL_UINT32(smallBytes, s_mediaLiveBytes);
  small = MicroGif{};
  TEST_ASSERT_EQUAL_UINT32(0, s_mediaLiveBytes);
}

void test_workspace_claim_survives_move_assignment_and_failed_rebegin() {
  MicroGif original, other;
  Canvas c(8, 8);
  int delayMs = 0;
  TEST_ASSERT_TRUE(original.begin(kGif8x8TwoFrames, kGif8x8TwoFrames_len, 8, 8));
  TEST_ASSERT_TRUE(original.nextFrame(c, delayMs) == MicroGif::Step::kFrame);
  TEST_ASSERT_TRUE(other.begin(kGif8x8TwoFrames, kGif8x8TwoFrames_len, 8, 8));
  TEST_ASSERT_TRUE(other.nextFrame(c, delayMs) == MicroGif::Step::kFrame);
  MicroGif moved(std::move(original));
  original = MicroGif{};
  other = std::move(moved);
  moved = MicroGif{};
  startMediaAllocProbe();
  const auto step = other.nextFrame(c, delayMs);
  s_trackMediaAllocs = false;
  TEST_ASSERT_TRUE(step == MicroGif::Step::kFrame);
  TEST_ASSERT_EQUAL_HEX32(0x00ff00, c.getPixel(7, 7));
  TEST_ASSERT_EQUAL_UINT32(0, s_mediaAllocs);
  TEST_ASSERT_EQUAL_UINT32(320, s_mediaLiveBytes);
  TEST_ASSERT_FALSE(other.begin(nullptr, 0, 8, 8));
  TEST_ASSERT_EQUAL_UINT32(0, s_mediaLiveBytes);
  TEST_ASSERT_TRUE(other.begin(kGif8x8TwoFrames, kGif8x8TwoFrames_len, 8, 8));
  TEST_ASSERT_TRUE(other.nextFrame(c, delayMs) == MicroGif::Step::kFrame);
  TEST_ASSERT_EQUAL_HEX32(0xff0000, c.getPixel(7, 7));
}

void test_panel_limits_reject_each_oversized_axis_before_allocating() {
  for (const auto dimensions : {std::pair<unsigned, unsigned>{52, 16}, {51, 17}}) {
    const auto data = fullPaletteGif(CodeBits{}, false, dimensions.first, dimensions.second);
    MicroGif g;
    TEST_ASSERT_TRUE(g.begin(data.data(), data.size(), 51, 16));
    TEST_ASSERT_EQUAL_INT(51, g.width());
    TEST_ASSERT_EQUAL_INT(16, g.height());
    Canvas c(51, 16);
    int delayMs = 0;
    startMediaAllocProbe();
    const auto step = g.nextFrame(c, delayMs);
    s_trackMediaAllocs = false;
    TEST_ASSERT_TRUE(step == MicroGif::Step::kError);
    TEST_ASSERT_EQUAL_UINT32(0, s_mediaAllocs);
  }
}

void test_invalid_panel_limits_and_overflowing_screen_rejected_without_allocating() {
  const auto huge = fullPaletteGif(CodeBits{}, false, 65535, 65535);
  for (const auto dimensions : {std::pair<int, int>{0, 8}, {8, 0}, {-1, 8}, {8, -1},
                                {std::numeric_limits<int>::max(),
                                 std::numeric_limits<int>::max()}}) {
    MicroGif g;
    startMediaAllocProbe();
    const bool opened = g.begin(huge.data(), huge.size(), dimensions.first, dimensions.second);
    s_trackMediaAllocs = false;
    TEST_ASSERT_FALSE(opened);
    TEST_ASSERT_EQUAL_UINT32(0, s_mediaAllocs);
  }
  auto hugeFrame = huge;
  hugeFrame[6] = hugeFrame[8] = 8;
  hugeFrame[7] = hugeFrame[9] = 0;
  MicroGif g;
  TEST_ASSERT_TRUE(g.begin(hugeFrame.data(), hugeFrame.size(), 65535, 65535));
  Canvas c(8, 8);
  int delayMs = 0;
  startMediaAllocProbe();
  const auto step = g.nextFrame(c, delayMs);
  s_trackMediaAllocs = false;
  TEST_ASSERT_TRUE(step == MicroGif::Step::kError);
  TEST_ASSERT_EQUAL_UINT32(0, s_mediaAllocs);
}

void test_large_frame_fills_4096_entry_dictionary_and_keeps_decoding() {
  constexpr unsigned width = 128, height = 40;
  CodeBits bits;
  bits.put(256);
  std::vector<uint8_t> expected;
  for (unsigned i = 0; i < 3839; ++i) {
    bits.put(i & 255, i < 255 ? 9 : i < 767 ? 10 : i < 1791 ? 11 : 12);
    expected.push_back(static_cast<uint8_t>(i));
  }
  // Entry 4095 contains literals 3837 and 3838. It must still be valid after the
  // dictionary fills, while following literals continue using 12-bit codes.
  bits.put(4095, 12);
  expected.push_back(3837 & 255);
  expected.push_back(3838 & 255);
  while (expected.size() < width * height) {
    const auto pixel = static_cast<uint8_t>(expected.size());
    bits.put(pixel, 12);
    expected.push_back(pixel);
  }
  bits.put(257, 12);
  const auto data = fullPaletteGif(bits, false, width, height);
  MicroGif g;
  TEST_ASSERT_TRUE(g.begin(data.data(), data.size(), width, height));
  Canvas c(width, height);
  int delayMs = 0;
  startMediaAllocProbe();
  const auto step = g.nextFrame(c, delayMs);
  s_trackMediaAllocs = false;
  TEST_ASSERT_TRUE(step == MicroGif::Step::kFrame);
  // Literal codes have no stored dictionary records; the expansion stack caps at 4096.
  TEST_ASSERT_EQUAL_UINT32(width * height + 3 * (4096 - 258) + 4096, s_mediaBytes);
  for (unsigned i = 0; i < expected.size(); ++i)
    TEST_ASSERT_EQUAL_HEX32(paletteColor(expected[i]), c.getPixel(i % width, i / width));
  TEST_ASSERT_TRUE(g.nextFrame(c, delayMs) == MicroGif::Step::kEnd);
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
  TEST_ASSERT_TRUE(g.begin(kIcon, sizeof(kIcon), 32, 8));
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
  RUN_TEST(test_restore_to_previous_recovers_pixels_under_transparent_delta);
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
  RUN_TEST(test_runtime_panel_dimensions_decode_41x8_and_51x16);
  RUN_TEST(test_interlaced_tall_frame_maps_all_four_passes);
  RUN_TEST(test_large_restore_previous_and_background_disposal);
  RUN_TEST(test_small_icon_working_memory_is_independent_of_panel_dimensions);
  RUN_TEST(test_decoder_allocation_failure_retries_the_same_frame);
  RUN_TEST(test_restore_snapshot_allocation_failure_returns_oom);
  RUN_TEST(test_oom_preserves_pending_transparency_delay_and_disposal);
  RUN_TEST(test_shared_workspace_is_reused_by_five_alternating_decoders);
  RUN_TEST(test_workspace_releases_large_capacity_when_largest_owner_closes);
  RUN_TEST(test_workspace_claim_survives_move_assignment_and_failed_rebegin);
  RUN_TEST(test_panel_limits_reject_each_oversized_axis_before_allocating);
  RUN_TEST(test_invalid_panel_limits_and_overflowing_screen_rejected_without_allocating);
  RUN_TEST(test_large_frame_fills_4096_entry_dictionary_and_keeps_decoding);
  return UNITY_END();
}
