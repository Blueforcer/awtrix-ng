
#include <unity.h>

#include <array>
#include <cstdlib>
#include <cstring>
#include <new>
#include <string>
#include <vector>

#include "core/render/Canvas.h"

namespace {
bool s_failBigAllocs = false;
constexpr std::size_t kFailThreshold = 10000;
bool s_trackAllocations = false;
std::size_t s_allocatedBytes = 0;
std::size_t s_allocationCount = 0;
std::size_t s_largestAllocation = 0;
std::size_t s_failExactBytes = 0;

std::size_t s_sweepMin = 2048;
int s_sweepNth = 0;
int s_sweepCount = 0;

bool shouldFail(std::size_t n) {
  if (s_trackAllocations) {
    s_allocatedBytes += n;
    ++s_allocationCount;
    if (n > s_largestAllocation) s_largestAllocation = n;
  }
  if (s_failExactBytes && n == s_failExactBytes) return true;
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
#include "sized_gif_fixture.h"

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
bool readAsset(const std::string& path, PodBuffer<uint8_t>& out, bool* outOfMemory) {
  (void)path;
  if (outOfMemory) *outOfMemory = false;
  if (!s_asset) return false;
  if (!out.resize(s_assetLen)) {
    if (outOfMemory) *outOfMemory = true;
    return false;
  }
  std::memcpy(out.data(), s_asset, s_assetLen);
  return true;
}
}
}

void setUp() {
  s_failBigAllocs = s_trackAllocations = false;
  s_failExactBytes = s_allocationCount = s_largestAllocation = 0;
  s_sweepNth = s_sweepCount = 0;
  s_sweepMin = 2048;
}
void tearDown() {
  s_failBigAllocs = s_trackAllocations = false;
  s_failExactBytes = 0;
  s_sweepNth = 0;
}

using awtrix::Canvas;
using awtrix::GifPlayer;
using OpenResult = GifPlayer::OpenResult;

bool openOk(GifPlayer& g, const std::string& id, bool firstFrameOnly = false) {
  return g.open(id, 32, 8, firstFrameOnly) == OpenResult::kGood;
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
  TEST_ASSERT_TRUE(gif.open("x", 32, 8, false, 1) ==
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
  TEST_ASSERT_TRUE(gif.open("x", 32, 8, false, 1) == OpenResult::kGood);
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
  TEST_ASSERT_TRUE(gif.open("x", 32, 8, false, 1) ==
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
  TEST_ASSERT_TRUE(gif.open("x", 32, 8) == OpenResult::kMissing);
  TEST_ASSERT_FALSE(gif.active());
}

void test_missing_asset_rejected() {
  useAsset(nullptr, 0);
  GifPlayer gif;
  TEST_ASSERT_TRUE(gif.open("x", 32, 8) == OpenResult::kMissing);
}

void test_asset_buffer_oom_is_reported_and_recovers_on_reopen() {
  useAsset(kGif8x8TwoFrames, kGif8x8TwoFrames_len);
  GifPlayer gif;
  s_failExactBytes = kGif8x8TwoFrames_len;
  const auto failed = gif.open("x", 8, 8, false, 1);
  s_failExactBytes = 0;
  TEST_ASSERT_TRUE(failed == OpenResult::kOom);
  TEST_ASSERT_FALSE(gif.active());
  TEST_ASSERT_EQUAL_INT(0, gif.width());
  TEST_ASSERT_TRUE(gif.open("x", 8, 8, false, 1) == OpenResult::kGood);
  Canvas c(8, 8);
  gif.render(c, 0);
  TEST_ASSERT_EQUAL_HEX32(0xff0000, c.getPixel(7, 7));
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

void test_base64_prefix_opens_inline() {
  std::string b64(encode_base64_length(kGif8x8TwoFrames_len), '\0');
  encode_base64(kGif8x8TwoFrames, kGif8x8TwoFrames_len,
                reinterpret_cast<unsigned char*>(&b64[0]));
  useAsset(nullptr, 0);
  GifPlayer gif;
  TEST_ASSERT_TRUE(openOk(gif, "base64:" + b64));
  Canvas c(8, 8);
  gif.render(c, 0);
  TEST_ASSERT_EQUAL_HEX32(0xFF0000u, c.getPixel(0, 0));
}

void test_short_base64_without_prefix_is_a_filename() {
  const std::string fake = "R0lGODlhAQABAIAAAAAAAAD/AAAAAAAsAAAAAAEAAQAAAgJEAQA7";
  TEST_ASSERT_TRUE(fake.size() < 64);
  useAsset(nullptr, 0);
  GifPlayer gif;
  TEST_ASSERT_TRUE(gif.open(fake, 32, 8) == OpenResult::kMissing);
}

void test_base64_prefix_forces_inline_for_short_strings() {
  const std::string fake(42, 'A');
  TEST_ASSERT_TRUE(fake.size() < 64);
  useAsset(kGif8x8TwoFrames, kGif8x8TwoFrames_len);

  // Without the prefix a short string is a filename, so the fixture answers the lookup.
  GifPlayer asFile;
  TEST_ASSERT_TRUE(openOk(asFile, fake));

  // With the prefix the same short string is inline data; the bytes are not a GIF, so it is
  // reported missing rather than ever reaching the filesystem.
  GifPlayer asInline;
  TEST_ASSERT_TRUE(asInline.open("base64:" + fake, 32, 8) == OpenResult::kMissing);
  TEST_ASSERT_FALSE(asInline.active());
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
  // The animation preflight intentionally bypasses an unneeded frame store. A forced
  // still frame larger than 10 KB genuinely requires the allocation tested here.
  const auto asset = sizedGif(51, 51, false);
  useAsset(asset.data(), asset.size());
  GifPlayer gif;
  s_failBigAllocs = true;
  const OpenResult r = gif.open("x", 51, 51, true);
  s_failBigAllocs = false;
  TEST_ASSERT_TRUE(r == OpenResult::kOom);
  TEST_ASSERT_FALSE(gif.active());
  TEST_ASSERT_EQUAL_INT(0, gif.width());

  TEST_ASSERT_TRUE(gif.open("x", 51, 51, true) == OpenResult::kGood);
  Canvas c(51, 51);
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
  s_sweepMin = 1;
  for (int nth = 1; nth <= 10; ++nth) {
    useAsset(fixture, len);
    GifPlayer gif;
    s_sweepCount = 0;
    s_sweepNth = nth;
    const bool ok = gif.open("x", 32, 8) == OpenResult::kGood;
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
  s_sweepMin = 2048;
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

void test_reopen_switches_between_streamed_and_cached_palettes() {
  GifPlayer gif;
  Canvas c(8, 8);
  for (int pass = 0; pass < 3; ++pass) {
    useAsset(kGifOddPalette, kGifOddPalette_len);
    TEST_ASSERT_TRUE(gif.open("odd", 32, 8, false, 1) == OpenResult::kGood);
    gif.render(c, 0);
    TEST_ASSERT_EQUAL_HEX32(kOddGlobal1, c.getPixel(0, 0));
    gif.render(c, 200);
    TEST_ASSERT_EQUAL_HEX32(kOddLocal1, c.getPixel(0, 0));

    useAsset(kGif8x8TwoFrames, kGif8x8TwoFrames_len);
    TEST_ASSERT_TRUE(openOk(gif, "cached"));
    gif.render(c, 0);
    TEST_ASSERT_EQUAL_HEX32(0xFF0000u, c.getPixel(0, 0));
    gif.render(c, 200);
    TEST_ASSERT_EQUAL_HEX32(0x00FF00u, c.getPixel(0, 0));
    gif.close();
  }
}

void test_panel_sized_gifs_cache_and_stream_at_native_dimensions() {
  for (const auto dims : {std::pair<int, int>{41, 8}, {51, 16}}) {
    const auto asset = sizedGif(dims.first, dims.second);
    useAsset(asset.data(), asset.size());
    for (int residentFrames : {0, 1}) {
      GifPlayer gif;
      TEST_ASSERT_TRUE(gif.open("wide", dims.first, dims.second, false, residentFrames) ==
                       OpenResult::kGood);
      TEST_ASSERT_EQUAL_INT(dims.first, gif.width());
      TEST_ASSERT_EQUAL_INT(dims.second, gif.height());
      Canvas c(dims.first, dims.second);
      gif.render(c, 0);
      TEST_ASSERT_EQUAL_HEX32(0xFF0000u, c.getPixel(dims.first - 1, dims.second - 1));
      gif.render(c, 100);
      TEST_ASSERT_EQUAL_HEX32(0x00FF00u, c.getPixel(dims.first - 1, dims.second - 1));
      gif.render(c, 200);
      TEST_ASSERT_EQUAL_HEX32(0xFF0000u, c.getPixel(dims.first - 1, dims.second - 1));
    }
  }
}

void test_gif_frames_larger_than_panel_are_rejected() {
  const auto asset = sizedGif(51, 16);
  useAsset(asset.data(), asset.size());
  GifPlayer gif;
  TEST_ASSERT_TRUE(gif.open("wide", 50, 16) == OpenResult::kMissing);
  TEST_ASSERT_FALSE(gif.active());
  TEST_ASSERT_TRUE(gif.open("tall", 51, 15) == OpenResult::kMissing);
  TEST_ASSERT_FALSE(gif.active());
  TEST_ASSERT_TRUE(gif.open("invalid", 0, 16) == OpenResult::kMissing);
}

void test_large_first_frame_only_does_not_animate() {
  const auto asset = sizedGif(129, 33);
  useAsset(asset.data(), asset.size());
  GifPlayer gif;
  TEST_ASSERT_TRUE(gif.open("still", 129, 33, true) == OpenResult::kGood);
  Canvas c(129, 33);
  gif.render(c, 0);
  TEST_ASSERT_EQUAL_HEX32(0xFF0000u, c.getPixel(128, 32));
  gif.render(c, 100);
  TEST_ASSERT_EQUAL_HEX32(0xFF0000u, c.getPixel(128, 32));
}

void test_small_gif_allocations_do_not_grow_with_panel() {
  useAsset(kGif8x8TwoFrames, kGif8x8TwoFrames_len);
  std::size_t baseline = 0;
  for (int panelSize : {8, 51, 1024}) {
    GifPlayer gif;
    s_allocatedBytes = 0;
    s_trackAllocations = true;
    const auto result = gif.open("small", panelSize, panelSize, false, 1);
    s_trackAllocations = false;
    TEST_ASSERT_TRUE(result == OpenResult::kGood);
    TEST_ASSERT_EQUAL_INT(8, gif.width());
    TEST_ASSERT_EQUAL_INT(8, gif.height());
    if (panelSize == 8) baseline = s_allocatedBytes;
    else TEST_ASSERT_EQUAL_UINT(baseline, s_allocatedBytes);
  }
}

void test_dynamic_scratch_oom_is_reported_and_recoverable() {
  const auto asset = sizedGif(51, 16);
  useAsset(asset.data(), asset.size());
  GifPlayer gif;
  for (std::size_t failingBytes : {std::size_t{51 * 16 * 5},
                                   51 * 16 * sizeof(uint32_t)}) {
    s_failExactBytes = failingBytes;
    const auto result = gif.open("wide", 51, 16);
    s_failExactBytes = 0;
    TEST_ASSERT_TRUE(result == OpenResult::kOom);
    TEST_ASSERT_FALSE(gif.active());
    TEST_ASSERT_EQUAL_INT(0, gif.width());
  }
  TEST_ASSERT_TRUE(gif.open("wide", 51, 16) == OpenResult::kGood);
  Canvas c(51, 16);
  gif.render(c, 0);
  TEST_ASSERT_EQUAL_HEX32(0xFF0000u, c.getPixel(50, 15));
}

void test_maximum_gif_delay_is_identical_cached_and_streamed() {
  auto asset = sizedGif(8, 8);
  // sizedGif's first GCE begins after the 13-byte header and 12-byte global palette.
  asset[29] = asset[30] = 0xff;
  useAsset(asset.data(), asset.size());
  for (int residentFrames : {0, 1}) {
    GifPlayer gif;
    TEST_ASSERT_TRUE(gif.open("x", 8, 8, false, residentFrames) == OpenResult::kGood);
    Canvas c(8, 8);
    gif.render(c, 0);
    gif.render(c, 65535);
    TEST_ASSERT_EQUAL_HEX32(0xff0000, c.getPixel(7, 7));
    gif.render(c, 655349);
    TEST_ASSERT_EQUAL_HEX32(0xff0000, c.getPixel(7, 7));
    gif.render(c, 655350);
    TEST_ASSERT_EQUAL_HEX32(0x00ff00, c.getPixel(7, 7));
  }
}

void test_zero_gif_delay_keeps_100ms_default_in_both_modes() {
  auto asset = sizedGif(8, 8);
  asset[29] = asset[30] = 0;
  useAsset(asset.data(), asset.size());
  for (int residentFrames : {0, 1}) {
    GifPlayer gif;
    TEST_ASSERT_TRUE(gif.open("x", 8, 8, false, residentFrames) == OpenResult::kGood);
    Canvas c(8, 8);
    gif.render(c, 0);
    gif.render(c, 99);
    TEST_ASSERT_EQUAL_HEX32(0xff0000, c.getPixel(0, 0));
    gif.render(c, 100);
    TEST_ASSERT_EQUAL_HEX32(0x00ff00, c.getPixel(0, 0));
  }
}

void test_streaming_preflight_reports_first_frame_decode_oom() {
  useAsset(kGif8x8TwoFrames, kGif8x8TwoFrames_len);
  for (const std::size_t bytes : {std::size_t{320}, std::size_t{8 * 8 * 4}}) {
    GifPlayer gif;
    s_failExactBytes = bytes;
    const auto result = gif.open("x", 8, 8, false, 1);
    s_failExactBytes = 0;
    TEST_ASSERT_TRUE(result == OpenResult::kOom);
    TEST_ASSERT_FALSE(gif.active());
    TEST_ASSERT_TRUE(gif.open("x", 8, 8, false, 1) == OpenResult::kGood);
    Canvas c(8, 8);
    gif.render(c, 0);
    TEST_ASSERT_EQUAL_HEX32(0xff0000, c.getPixel(7, 7));
  }
}

void test_streaming_preflight_keeps_only_initial_pixels_and_shared_scratch() {
  useAsset(kGif8x8TwoFrames, kGif8x8TwoFrames_len);
  GifPlayer gif;
  s_allocatedBytes = s_allocationCount = 0;
  s_trackAllocations = true;
  const auto result = gif.open("x", 8, 8, false, 1);
  s_trackAllocations = false;
  TEST_ASSERT_TRUE(result == OpenResult::kGood);
  // Compressed asset, one initial RGB frame and the shared five-bytes-per-pixel
  // workspace suffice. No duplicate RGB cache or separate delay array is needed.
  constexpr std::size_t pixels = 8 * 8;
  TEST_ASSERT_LESS_OR_EQUAL_UINT32(3, s_allocationCount);
  TEST_ASSERT_LESS_OR_EQUAL_UINT32(kGif8x8TwoFrames_len + pixels * (sizeof(uint32_t) + 5),
                                  s_allocatedBytes);
  awtrix::media::PodBuffer<uint32_t> initial;
  TEST_ASSERT_TRUE(gif.takeInitialFrame(initial));
  TEST_ASSERT_EQUAL_UINT32(pixels, initial.size());
  TEST_ASSERT_EQUAL_HEX32(0xff0000, initial[pixels - 1]);
}

void test_streaming_preflight_rejects_invalid_first_lzw_frame() {
  auto asset = sizedGif(8, 8);
  // Header + palette + GCE + descriptor + min-code-size + block length = 45.
  // First 3-bit code 7 is undefined before any literal or dictionary entry.
  asset[45] = static_cast<uint8_t>((asset[45] & ~7u) | 7u);
  useAsset(asset.data(), asset.size());
  GifPlayer gif;
  TEST_ASSERT_TRUE(gif.open("x", 8, 8, false, 1) == OpenResult::kMissing);
  TEST_ASSERT_FALSE(gif.active());
  TEST_ASSERT_EQUAL_INT(0, gif.width());
}

void test_streaming_initial_frame_transfer_continues_animation_without_copying() {
  useAsset(kGif8x8TwoFrames, kGif8x8TwoFrames_len);
  GifPlayer gif;
  TEST_ASSERT_TRUE(gif.open("x", 8, 8, false, 1) == OpenResult::kGood);
  awtrix::media::PodBuffer<uint32_t> pixels;
  s_allocationCount = 0;
  s_trackAllocations = true;
  const bool taken = gif.takeInitialFrame(pixels);
  s_trackAllocations = false;
  TEST_ASSERT_TRUE(taken);
  TEST_ASSERT_EQUAL_UINT32(0, s_allocationCount);
  TEST_ASSERT_EQUAL_UINT32(64, pixels.size());
  TEST_ASSERT_EQUAL_HEX32(0xff0000, pixels[63]);
  TEST_ASSERT_TRUE(gif.active());
  TEST_ASSERT_FALSE(gif.takeInitialFrame(pixels));
  TEST_ASSERT_FALSE(gif.takeStaticFrame(pixels));

  Canvas c(8, 8, pixels.data());
  // Changing a pixel proves the first render only starts the delay after transfer,
  // without copying or decoding the already transferred first frame again.
  pixels[0] = 0x123456;
  s_allocationCount = 0;
  s_trackAllocations = true;
  gif.render(c, 5000);
  gif.render(c, 5199);
  s_trackAllocations = false;
  TEST_ASSERT_EQUAL_UINT32(0, s_allocationCount);
  TEST_ASSERT_EQUAL_HEX32(0x123456, pixels[0]);
  TEST_ASSERT_EQUAL_HEX32(0xff0000, pixels[63]);
  gif.render(c, 5200);
  TEST_ASSERT_EQUAL_HEX32(0x00ff00, pixels[0]);
  TEST_ASSERT_EQUAL_HEX32(0x00ff00, pixels[63]);
  gif.render(c, 5500);
  TEST_ASSERT_EQUAL_HEX32(0xff0000, pixels[63]);
}

void test_initial_frame_transfer_rejects_cached_images_without_changing_output() {
  useAsset(kGif8x8TwoFrames, kGif8x8TwoFrames_len);
  GifPlayer gif;
  TEST_ASSERT_TRUE(gif.open("x", 8, 8) == OpenResult::kGood);
  awtrix::media::PodBuffer<uint32_t> pixels;
  TEST_ASSERT_TRUE(pixels.resize(1));
  pixels[0] = 0x123456;
  TEST_ASSERT_FALSE(gif.takeInitialFrame(pixels));
  TEST_ASSERT_EQUAL_UINT32(1, pixels.size());
  TEST_ASSERT_EQUAL_HEX32(0x123456, pixels[0]);
  TEST_ASSERT_TRUE(gif.active());
}

void test_five_streaming_players_allocate_nothing_after_warmup() {
  const auto asset = sizedGif(41, 8);
  useAsset(asset.data(), asset.size());
  std::array<GifPlayer, 5> players;
  std::array<Canvas, 5> canvases = {Canvas(41, 8), Canvas(41, 8), Canvas(41, 8),
                                  Canvas(41, 8), Canvas(41, 8)};
  for (unsigned i = 0; i < players.size(); ++i) {
    TEST_ASSERT_TRUE(players[i].open("x", 41, 8, false, 1) == OpenResult::kGood);
    players[i].render(canvases[i], 0);
  }
  s_allocationCount = s_allocatedBytes = 0;
  s_trackAllocations = true;
  for (int frame = 1; frame <= 20; ++frame)
    for (unsigned i = 0; i < players.size(); ++i) {
      players[i].render(canvases[i], frame * 100);
      TEST_ASSERT_EQUAL_HEX32(frame % 2 ? 0x00ff00 : 0xff0000, canvases[i].getPixel(40, 7));
    }
  s_trackAllocations = false;
  TEST_ASSERT_EQUAL_UINT32(0, s_allocationCount);
  TEST_ASSERT_EQUAL_UINT32(0, s_allocatedBytes);
}

void test_streaming_oom_keeps_last_frame_and_retries_pending_frame() {
  auto asset = sizedGif(8, 8);
  // First frame disposes to background; second needs restore-to-previous. Fail that
  // snapshot after the shared scratch has warmed up on the first frame.
  asset[28] = 8;
  for (std::size_t i = 33; i + 7 < asset.size(); ++i)
    if (asset[i] == 0x21 && asset[i + 1] == 0xf9 && asset[i + 2] == 4) {
      asset[i + 3] = 12;
      break;
    }
  useAsset(asset.data(), asset.size());
  GifPlayer gif;
  TEST_ASSERT_TRUE(gif.open("x", 8, 8, false, 1) == OpenResult::kGood);
  Canvas c(8, 8);
  gif.render(c, 0);
  s_failExactBytes = 8 * 8 * 4;
  gif.render(c, 100);
  TEST_ASSERT_EQUAL_HEX32(0xff0000, c.getPixel(7, 7));
  gif.render(c, 1100);
  TEST_ASSERT_EQUAL_HEX32(0xff0000, c.getPixel(7, 7));
  s_failExactBytes = 0;
  gif.render(c, 2100);
  TEST_ASSERT_EQUAL_HEX32(0x00ff00, c.getPixel(7, 7));
  gif.render(c, 2200);
  TEST_ASSERT_EQUAL_HEX32(0xff0000, c.getPixel(7, 7));
}

void test_static_frame_transfers_without_allocating_and_survives_player_close() {
  const auto asset = sizedGif(41, 8, false);
  useAsset(asset.data(), asset.size());
  GifPlayer gif;
  TEST_ASSERT_TRUE(gif.open("x", 41, 8, false, 1) == OpenResult::kGood);
  awtrix::media::PodBuffer<uint32_t> pixels;
  s_allocationCount = 0;
  s_trackAllocations = true;
  const bool taken = gif.takeStaticFrame(pixels);
  s_trackAllocations = false;
  TEST_ASSERT_TRUE(taken);
  TEST_ASSERT_EQUAL_UINT32(0, s_allocationCount);
  TEST_ASSERT_EQUAL_UINT32(41 * 8, pixels.size());
  TEST_ASSERT_FALSE(gif.active());
  TEST_ASSERT_EQUAL_INT(41, gif.width());
  TEST_ASSERT_FALSE(gif.takeStaticFrame(pixels));
  gif.close();
  for (std::size_t i = 0; i < pixels.size(); ++i) TEST_ASSERT_EQUAL_HEX32(0xff0000, pixels[i]);
}

void test_single_frame_beyond_the_cache_budget_is_still_a_static_image() {
  const auto asset = sizedGif(128, 64, false);
  useAsset(asset.data(), asset.size());
  GifPlayer gif;
  TEST_ASSERT_TRUE(gif.open("x", 128, 64, false, 1) == OpenResult::kGood);
  awtrix::media::PodBuffer<uint32_t> pixels;
  TEST_ASSERT_TRUE(gif.takeStaticFrame(pixels));
  TEST_ASSERT_EQUAL_UINT32(128 * 64, pixels.size());
  TEST_ASSERT_FALSE(gif.active());
  TEST_ASSERT_EQUAL_HEX32(0xff0000, pixels[128 * 64 - 1]);
}

void test_static_transfer_rejects_animation_without_modifying_output() {
  const auto asset = sizedGif(8, 8);
  useAsset(asset.data(), asset.size());
  for (int residentFrames : {0, 1}) {
    GifPlayer gif;
    TEST_ASSERT_TRUE(gif.open("x", 8, 8, false, residentFrames) == OpenResult::kGood);
    awtrix::media::PodBuffer<uint32_t> pixels;
    TEST_ASSERT_TRUE(pixels.resize(1));
    pixels[0] = 0x123456;
    TEST_ASSERT_FALSE(gif.takeStaticFrame(pixels));
    TEST_ASSERT_TRUE(gif.active());
    TEST_ASSERT_EQUAL_UINT32(1, pixels.size());
    TEST_ASSERT_EQUAL_HEX32(0x123456, pixels[0]);
  }
}

void test_frame_cache_growth_stays_within_budget_at_51x16() {
  const auto single = sizedGif(51, 16, false);
  auto asset = single;
  // Exactly five frames fit the 16 KB cache (5 * 51 * 16 * 4 = 16320 bytes).
  for (int frame = 1; frame < 5; ++frame) {
    asset.pop_back();
    asset.insert(asset.end(), single.begin() + 25, single.end());
  }
  useAsset(asset.data(), asset.size());
  GifPlayer gif;
  s_largestAllocation = 0;
  s_trackAllocations = true;
  const auto result = gif.open("x", 51, 16);
  s_trackAllocations = false;
  TEST_ASSERT_TRUE(result == OpenResult::kGood);
  TEST_ASSERT_LESS_OR_EQUAL_UINT32(16 * 1024, s_largestAllocation);
  Canvas c(51, 16);
  s_allocationCount = 0;
  s_trackAllocations = true;
  for (int frame = 0; frame < 7; ++frame) gif.render(c, frame * 100);
  s_trackAllocations = false;
  TEST_ASSERT_EQUAL_UINT32(0, s_allocationCount);
  TEST_ASSERT_EQUAL_HEX32(0xff0000, c.getPixel(50, 15));
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
  RUN_TEST(test_asset_buffer_oom_is_reported_and_recovers_on_reopen);
  RUN_TEST(test_inline_base64_gif);
  RUN_TEST(test_base64_prefix_opens_inline);
  RUN_TEST(test_short_base64_without_prefix_is_a_filename);
  RUN_TEST(test_base64_prefix_forces_inline_for_short_strings);
  RUN_TEST(test_transparent_static_renders_black);
  RUN_TEST(test_transparent_animation_keeps_previous_frame);
  RUN_TEST(test_transparent_streaming_first_frame_is_black);
  RUN_TEST(test_frame_store_oom_reports_koom_not_a_crash);
  RUN_TEST(test_small_icon_needs_no_big_allocation);
  RUN_TEST(test_alloc_failure_sweep_predecoded_path);
  RUN_TEST(test_alloc_failure_sweep_streaming_path);
  RUN_TEST(test_reopen_after_close);
  RUN_TEST(test_reopen_switches_between_streamed_and_cached_palettes);
  RUN_TEST(test_panel_sized_gifs_cache_and_stream_at_native_dimensions);
  RUN_TEST(test_gif_frames_larger_than_panel_are_rejected);
  RUN_TEST(test_large_first_frame_only_does_not_animate);
  RUN_TEST(test_small_gif_allocations_do_not_grow_with_panel);
  RUN_TEST(test_dynamic_scratch_oom_is_reported_and_recoverable);
  RUN_TEST(test_maximum_gif_delay_is_identical_cached_and_streamed);
  RUN_TEST(test_zero_gif_delay_keeps_100ms_default_in_both_modes);
  RUN_TEST(test_streaming_preflight_reports_first_frame_decode_oom);
  RUN_TEST(test_streaming_preflight_keeps_only_initial_pixels_and_shared_scratch);
  RUN_TEST(test_streaming_preflight_rejects_invalid_first_lzw_frame);
  RUN_TEST(test_streaming_initial_frame_transfer_continues_animation_without_copying);
  RUN_TEST(test_initial_frame_transfer_rejects_cached_images_without_changing_output);
  RUN_TEST(test_five_streaming_players_allocate_nothing_after_warmup);
  RUN_TEST(test_streaming_oom_keeps_last_frame_and_retries_pending_frame);
  RUN_TEST(test_static_frame_transfers_without_allocating_and_survives_player_close);
  RUN_TEST(test_single_frame_beyond_the_cache_budget_is_still_a_static_image);
  RUN_TEST(test_static_transfer_rejects_animation_without_modifying_output);
  RUN_TEST(test_frame_cache_growth_stays_within_budget_at_51x16);
  return UNITY_END();
}
