
#include <unity.h>

#include <cstdlib>
#include <cstring>
#include <new>
#include <string>
#include <vector>

#include "core/render/Canvas.h"

namespace {
bool s_failBigAllocs = false;
constexpr std::size_t kFailThreshold = 10000;

std::size_t s_sweepMin = 2048;
int s_sweepNth = 0;
int s_sweepCount = 0;

bool shouldFail(std::size_t n) {
  if (s_failBigAllocs && n >= kFailThreshold) return true;
  if (s_sweepNth > 0 && n >= s_sweepMin && ++s_sweepCount == s_sweepNth) return true;
  return false;
}
}

void* operator new(std::size_t n) {
  if (!shouldFail(n)) {
    if (void* p = std::malloc(n)) return p;
  }
  throw std::bad_alloc();
}
void* operator new(std::size_t n, const std::nothrow_t&) noexcept {
  if (shouldFail(n)) return nullptr;
  return std::malloc(n);
}
void* operator new[](std::size_t n) { return operator new(n); }
void* operator new[](std::size_t n, const std::nothrow_t&) noexcept {
  return operator new(n, std::nothrow);
}
void operator delete(void* p) noexcept { std::free(p); }
void operator delete(void* p, std::size_t) noexcept { std::free(p); }
void operator delete(void* p, const std::nothrow_t&) noexcept { std::free(p); }
void operator delete[](void* p) noexcept { std::free(p); }
void operator delete[](void* p, std::size_t) noexcept { std::free(p); }
void operator delete[](void* p, const std::nothrow_t&) noexcept { std::free(p); }

#include "../../src/media/GifPlayer.cpp"
#include "../../src/media/MicroGif.cpp"

#include <base64.hpp>

#include "gif_fixtures.h"

namespace {

const unsigned char* s_asset = nullptr;
unsigned int s_assetLen = 0;

void useAsset(const unsigned char* data, unsigned int len) {
  s_asset = data;
  s_assetLen = len;
}

}

namespace awtrix {
namespace media {
bool readAsset(const std::string& path, PodBuffer<uint8_t>& out) {
  (void)path;
  if (!s_asset) return false;
  if (!out.resize(s_assetLen)) return false;
  std::memcpy(out.data(), s_asset, s_assetLen);
  return true;
}
}
}

void setUp() {}
void tearDown() {}

using awtrix::Canvas;
using awtrix::GifPlayer;
using OpenResult = GifPlayer::OpenResult;

bool openOk(GifPlayer& g, const std::string& id, bool firstFrameOnly = false) {
  return g.open(id, firstFrameOnly) == OpenResult::kGood;
}

void test_two_frame_playback_and_loop() {
  useAsset(kGif8x8TwoFrames, kGif8x8TwoFrames_len);
  GifPlayer gif;
  TEST_ASSERT_TRUE(openOk(gif, "x"));
  TEST_ASSERT_TRUE(gif.active());
  TEST_ASSERT_EQUAL_INT(8, gif.width());
  TEST_ASSERT_EQUAL_INT(8, gif.height());

  Canvas c(8, 8);
  gif.render(c, 0);
  TEST_ASSERT_EQUAL_HEX32(0xFF0000u, c.getPixel(0, 0));
  TEST_ASSERT_EQUAL_HEX32(0xFF0000u, c.getPixel(7, 7));

  gif.render(c, 100);
  TEST_ASSERT_EQUAL_HEX32(0xFF0000u, c.getPixel(0, 0));

  gif.render(c, 200);
  TEST_ASSERT_EQUAL_HEX32(0x00FF00u, c.getPixel(0, 0));

  gif.render(c, 500);
  TEST_ASSERT_EQUAL_HEX32(0xFF0000u, c.getPixel(3, 4));
}

void test_32x8_frames_keep_full_width() {
  useAsset(kGif32x8TwoFrames, kGif32x8TwoFrames_len);
  GifPlayer gif;
  TEST_ASSERT_TRUE(openOk(gif, "x"));
  TEST_ASSERT_EQUAL_INT(32, gif.width());
  TEST_ASSERT_EQUAL_INT(8, gif.height());

  Canvas c(32, 8);
  gif.render(c, 0);
  TEST_ASSERT_EQUAL_HEX32(0x0000FFu, c.getPixel(0, 0));
  TEST_ASSERT_EQUAL_HEX32(0x0000FFu, c.getPixel(31, 7));
}

void test_first_frame_only() {
  useAsset(kGif8x8TwoFrames, kGif8x8TwoFrames_len);
  GifPlayer gif;
  TEST_ASSERT_TRUE(openOk(gif, "x", true));

  Canvas c(8, 8);
  gif.render(c, 0);
  TEST_ASSERT_EQUAL_HEX32(0xFF0000u, c.getPixel(0, 0));
  gif.render(c, 10000);
  TEST_ASSERT_EQUAL_HEX32(0xFF0000u, c.getPixel(0, 0));
}

void test_long_animation_streams_and_still_plays() {
  useAsset(kGif32x8ManyFrames, kGif32x8ManyFrames_len);
  GifPlayer gif;
  TEST_ASSERT_TRUE(openOk(gif, "x"));
  TEST_ASSERT_TRUE(gif.active());

  Canvas c(32, 8);
  long now = 0;
  gif.render(c, now);
  TEST_ASSERT_EQUAL_HEX32(0xFF0000u, c.getPixel(16, 4));
  now += 50;
  gif.render(c, now);
  TEST_ASSERT_EQUAL_HEX32(0x0000FFu, c.getPixel(16, 4));
}

void test_max_resident_frames_forces_streaming() {
  useAsset(kGif8x8TwoFrames, kGif8x8TwoFrames_len);
  GifPlayer gif;
  TEST_ASSERT_TRUE(gif.open("x", false, 1) ==
                   OpenResult::kGood);
  TEST_ASSERT_TRUE(gif.active());

  Canvas c(8, 8);
  gif.render(c, 0);
  TEST_ASSERT_EQUAL_HEX32(0xFF0000u, c.getPixel(0, 0));
  gif.render(c, 200);
  TEST_ASSERT_EQUAL_HEX32(0x00FF00u, c.getPixel(0, 0));
  gif.render(c, 500);
  TEST_ASSERT_EQUAL_HEX32(0xFF0000u, c.getPixel(3, 4));
}

void test_max_resident_frames_keeps_static_resident() {
  useAsset(kGifTransparentStatic, kGifTransparentStatic_len);
  GifPlayer gif;
  TEST_ASSERT_TRUE(gif.open("x", false, 1) == OpenResult::kGood);
  Canvas c(8, 8);
  c.clear(0xFFFFFFu);
  gif.render(c, 0);
  TEST_ASSERT_EQUAL_HEX32(0xFF0000u, c.getPixel(0, 0));
  gif.render(c, 10000);
  TEST_ASSERT_EQUAL_HEX32(0xFF0000u, c.getPixel(0, 0));
}

void test_odd_palette_survives_the_frame_cache() {
  useAsset(kGifOddPalette, kGifOddPalette_len);
  GifPlayer gif;
  TEST_ASSERT_TRUE(openOk(gif, "x"));

  Canvas c(8, 8);
  gif.render(c, 0);
  TEST_ASSERT_EQUAL_HEX32(kOddGlobal1, c.getPixel(0, 0));
  TEST_ASSERT_EQUAL_HEX32(kOddGlobal0, c.getPixel(7, 7));

  gif.render(c, 200);
  TEST_ASSERT_EQUAL_HEX32(kOddLocal1, c.getPixel(0, 0));
  TEST_ASSERT_EQUAL_HEX32(kOddLocal0, c.getPixel(7, 7));
}

void test_odd_palette_survives_streaming() {
  useAsset(kGifOddPalette, kGifOddPalette_len);
  GifPlayer gif;
  TEST_ASSERT_TRUE(gif.open("x", false, 1) ==
                   OpenResult::kGood);

  Canvas c(8, 8);
  gif.render(c, 0);
  TEST_ASSERT_EQUAL_HEX32(kOddGlobal1, c.getPixel(0, 0));
  TEST_ASSERT_EQUAL_HEX32(kOddGlobal0, c.getPixel(7, 7));

  gif.render(c, 200);
  TEST_ASSERT_EQUAL_HEX32(kOddLocal1, c.getPixel(0, 0));
  TEST_ASSERT_EQUAL_HEX32(kOddLocal0, c.getPixel(7, 7));
}

void test_non_gif_rejected() {
  static const unsigned char junk[] = "JFIF definitely not a gif, long enough";
  useAsset(junk, sizeof(junk));
  GifPlayer gif;
  TEST_ASSERT_TRUE(gif.open("x") == OpenResult::kMissing);
  TEST_ASSERT_FALSE(gif.active());
}

void test_missing_asset_rejected() {
  useAsset(nullptr, 0);
  GifPlayer gif;
  TEST_ASSERT_TRUE(gif.open("x") == OpenResult::kMissing);
}

void test_inline_base64_gif() {
  std::string b64(encode_base64_length(kGif8x8TwoFrames_len), '\0');
  encode_base64(kGif8x8TwoFrames, kGif8x8TwoFrames_len,
                reinterpret_cast<unsigned char*>(&b64[0]));
  TEST_ASSERT_TRUE(b64.size() > 64);
  useAsset(nullptr, 0);
  GifPlayer gif;
  TEST_ASSERT_TRUE(openOk(gif, b64));
  Canvas c(8, 8);
  gif.render(c, 0);
  TEST_ASSERT_EQUAL_HEX32(0xFF0000u, c.getPixel(0, 0));
}

void test_transparent_static_renders_black() {
  useAsset(kGifTransparentStatic, kGifTransparentStatic_len);
  GifPlayer gif;
  TEST_ASSERT_TRUE(openOk(gif, "x"));

  Canvas c(8, 8);
  c.clear(0xFFFFFFu);
  gif.render(c, 0);
  TEST_ASSERT_EQUAL_HEX32(0xFF0000u, c.getPixel(0, 0));
  TEST_ASSERT_EQUAL_HEX32(0xFF0000u, c.getPixel(3, 7));
  TEST_ASSERT_EQUAL_HEX32(0x000000u, c.getPixel(4, 0));
  TEST_ASSERT_EQUAL_HEX32(0x000000u, c.getPixel(7, 7));
}

void test_transparent_animation_keeps_previous_frame() {
  useAsset(kGifTransparentAnim, kGifTransparentAnim_len);
  GifPlayer gif;
  TEST_ASSERT_TRUE(openOk(gif, "x"));

  Canvas c(8, 8);
  c.clear(0xFFFFFFu);
  gif.render(c, 0);
  TEST_ASSERT_EQUAL_HEX32(0x00FF00u, c.getPixel(0, 0));
  TEST_ASSERT_EQUAL_HEX32(0x00FF00u, c.getPixel(7, 7));

  gif.render(c, 200);
  TEST_ASSERT_EQUAL_HEX32(0x0000FFu, c.getPixel(0, 0));
  TEST_ASSERT_EQUAL_HEX32(0x0000FFu, c.getPixel(3, 3));
  TEST_ASSERT_EQUAL_HEX32(0x00FF00u, c.getPixel(7, 7));
}

void test_transparent_streaming_first_frame_is_black() {
  useAsset(kGifTransparentManyFrames, kGifTransparentManyFrames_len);
  GifPlayer gif;
  TEST_ASSERT_TRUE(openOk(gif, "x"));

  Canvas c(32, 8);
  c.clear(0xFFFFFFu);
  gif.render(c, 0);
  TEST_ASSERT_EQUAL_HEX32(0xFF0000u, c.getPixel(0, 0));
  TEST_ASSERT_EQUAL_HEX32(0x000000u, c.getPixel(31, 0));
  TEST_ASSERT_EQUAL_HEX32(0x000000u, c.getPixel(16, 7));
}

void test_frame_store_oom_reports_koom_not_a_crash() {
  useAsset(kGif32x8ManyFrames, kGif32x8ManyFrames_len);
  GifPlayer gif;
  s_failBigAllocs = true;
  const OpenResult r = gif.open("x");
  s_failBigAllocs = false;
  TEST_ASSERT_TRUE(r == OpenResult::kOom);
  TEST_ASSERT_FALSE(gif.active());
  TEST_ASSERT_EQUAL_INT(0, gif.width());

  TEST_ASSERT_TRUE(openOk(gif, "x"));
  Canvas c(32, 8);
  gif.render(c, 0);
  TEST_ASSERT_EQUAL_HEX32(0xFF0000u, c.getPixel(16, 4));
}

void test_small_icon_needs_no_big_allocation() {
  useAsset(kGif8x8TwoFrames, kGif8x8TwoFrames_len);
  GifPlayer gif;
  s_failBigAllocs = true;
  const bool ok = openOk(gif, "x");
  s_failBigAllocs = false;
  TEST_ASSERT_TRUE(ok);
  Canvas c(8, 8);
  gif.render(c, 0);
  TEST_ASSERT_EQUAL_HEX32(0xFF0000u, c.getPixel(0, 0));
}

void sweepOpen(const unsigned char* fixture, unsigned int len) {
  for (int nth = 1; nth <= 10; ++nth) {
    useAsset(fixture, len);
    GifPlayer gif;
    s_sweepCount = 0;
    s_sweepNth = nth;
    const bool ok = gif.open("x") == OpenResult::kGood;
    s_sweepNth = 0;
    if (ok) {
      Canvas c(32, 8);
      gif.render(c, 0);
    } else {
      TEST_ASSERT_FALSE(gif.active());
      TEST_ASSERT_EQUAL_INT(0, gif.width());
    }
    gif.close();
    TEST_ASSERT_TRUE(openOk(gif, "x"));
    Canvas c(32, 8);
    gif.render(c, 0);
  }
}

void test_alloc_failure_sweep_predecoded_path() {
  sweepOpen(kGif8x8TwoFrames, kGif8x8TwoFrames_len);
}

void test_alloc_failure_sweep_streaming_path() {
  sweepOpen(kGif32x8ManyFrames, kGif32x8ManyFrames_len);
}

void test_reopen_after_close() {
  useAsset(kGif8x8TwoFrames, kGif8x8TwoFrames_len);
  GifPlayer gif;
  TEST_ASSERT_TRUE(openOk(gif, "x"));
  gif.close();
  TEST_ASSERT_FALSE(gif.active());
  TEST_ASSERT_EQUAL_INT(0, gif.width());
  TEST_ASSERT_TRUE(openOk(gif, "x"));
  Canvas c(8, 8);
  gif.render(c, 0);
  TEST_ASSERT_EQUAL_HEX32(0xFF0000u, c.getPixel(0, 0));
}

int main(int, char**) {
  UNITY_BEGIN();
  RUN_TEST(test_two_frame_playback_and_loop);
  RUN_TEST(test_32x8_frames_keep_full_width);
  RUN_TEST(test_first_frame_only);
  RUN_TEST(test_long_animation_streams_and_still_plays);
  RUN_TEST(test_max_resident_frames_forces_streaming);
  RUN_TEST(test_max_resident_frames_keeps_static_resident);
  RUN_TEST(test_odd_palette_survives_the_frame_cache);
  RUN_TEST(test_odd_palette_survives_streaming);
  RUN_TEST(test_non_gif_rejected);
  RUN_TEST(test_missing_asset_rejected);
  RUN_TEST(test_inline_base64_gif);
  RUN_TEST(test_transparent_static_renders_black);
  RUN_TEST(test_transparent_animation_keeps_previous_frame);
  RUN_TEST(test_transparent_streaming_first_frame_is_black);
  RUN_TEST(test_frame_store_oom_reports_koom_not_a_crash);
  RUN_TEST(test_small_icon_needs_no_big_allocation);
  RUN_TEST(test_alloc_failure_sweep_predecoded_path);
  RUN_TEST(test_alloc_failure_sweep_streaming_path);
  RUN_TEST(test_reopen_after_close);
  return UNITY_END();
}
