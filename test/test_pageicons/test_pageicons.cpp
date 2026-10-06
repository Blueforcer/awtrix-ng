#include <unity.h>

#include <cstdlib>
#include <cstring>
#include <new>
#include <string>
#include <vector>

#include "core/render/Canvas.h"

namespace {
bool s_trackPixels = false;
int s_pixelAllocations = 0;
}

void* operator new(std::size_t bytes) {
  if (s_trackPixels && bytes == 8 * 8 * sizeof(uint32_t)) ++s_pixelAllocations;
  if (void* p = std::malloc(bytes)) return p;
  throw std::bad_alloc();
}
void* operator new(std::size_t bytes, const std::nothrow_t&) noexcept {
  if (s_trackPixels && bytes == 8 * 8 * sizeof(uint32_t)) ++s_pixelAllocations;
  return std::malloc(bytes);
}
void* operator new[](std::size_t bytes) { return operator new(bytes); }
void* operator new[](std::size_t bytes, const std::nothrow_t&) noexcept {
  return operator new(bytes, std::nothrow);
}
void operator delete(void* p) noexcept { std::free(p); }
void operator delete(void* p, std::size_t) noexcept { std::free(p); }
void operator delete(void* p, const std::nothrow_t&) noexcept { std::free(p); }
void operator delete[](void* p) noexcept { std::free(p); }
void operator delete[](void* p, std::size_t) noexcept { std::free(p); }
void operator delete[](void* p, const std::nothrow_t&) noexcept { std::free(p); }

#include "../../src/media/GifPlayer.cpp"
#include "../../src/media/MicroGif.cpp"
#include "media/DevicePageIcon.h"
#include "platform/linux/images/RemotePageIcon.h"

#include <base64.hpp>

#include "../test_gifplayer/sized_gif_fixture.h"
#include "fake_remote_images.h"

namespace {
std::vector<uint8_t> s_fast, s_slow, s_still;
bool s_jpgOom = false;
int s_assetReads = 0;
int s_jpgDraws = 0;
}

namespace awtrix {
namespace media {
bool readAssetRange(std::string_view, size_t, uint8_t*, size_t,
                    size_t& bytesRead, size_t& fileSize) {
  ++s_assetReads;
  bytesRead = fileSize = 0;
  return false;
}
bool readAsset(std::string_view path, PodBuffer<uint8_t>& out, bool* outOfMemory) {
  ++s_assetReads;
  if (outOfMemory) *outOfMemory = false;
  const auto* asset = path == "/ICONS/fast.gif" ? &s_fast :
                      path == "/ICONS/slow.gif" ? &s_slow :
                      path == "/ICONS/still.gif" ? &s_still : nullptr;
  if (!asset) return false;
  if (!out.resize(asset->size())) {
    if (outOfMemory) *outOfMemory = true;
    return false;
  }
  std::memcpy(out.data(), asset->data(), asset->size());
  return true;
}
}
namespace icon {
bool decodeNative(const uint8_t*, std::size_t, int, int, media::PodBuffer<uint32_t>&,
                  int& width, int& height, bool* outOfMemory) {
  width = height = 0;
  if (outOfMemory) *outOfMemory = s_jpgOom;
  return false;
}

bool draw(Canvas&, std::string_view, int, int, bool* outOfMemory) {
  ++s_jpgDraws;
  if (outOfMemory) *outOfMemory = s_jpgOom;
  return false;
}
}
}

using awtrix::Canvas;
using awtrix::DevicePageIcon;
using awtrix::RemotePageIcon;

void setUp() {
  s_fast = sizedGif(8, 8);
  s_slow = s_fast;
  s_still = sizedGif(8, 8, false);
  for (std::size_t i = 0; i + 7 < s_slow.size(); ++i) {
    if (s_slow[i] == 0x21 && s_slow[i + 1] == 0xf9 && s_slow[i + 2] == 4)
      s_slow[i + 4] = 20;  // 200 ms instead of 100 ms.
  }
  s_pixelAllocations = 0;
  s_trackPixels = false;
  s_assetReads = 0;
  s_jpgDraws = 0;
}
void tearDown() {
  s_trackPixels = false;
  s_jpgOom = false;
}

void test_multiple_page_icons_keep_independent_animation_schedules() {
  DevicePageIcon factory;
  auto fast = factory.create();
  auto slow = factory.create();
  TEST_ASSERT_NOT_NULL(fast.get());
  TEST_ASSERT_NOT_NULL(slow.get());
  TEST_ASSERT_TRUE(fast->begin("fast", 32, 8) == awtrix::IconLoad::kGood);
  TEST_ASSERT_TRUE(slow->begin("slow", 32, 8) == awtrix::IconLoad::kGood);
  Canvas canvas(32, 8);
  auto frame = [&](int64_t time) {
    canvas.clear();
    fast->advance(time);
    slow->advance(time);
    fast->blit(canvas, 0, 0);
    slow->blit(canvas, 16, 0);
  };
  frame(0);
  TEST_ASSERT_EQUAL_HEX32(0xFF0000u, canvas.getPixel(0, 0));
  TEST_ASSERT_EQUAL_HEX32(0xFF0000u, canvas.getPixel(16, 0));
  frame(100);
  TEST_ASSERT_EQUAL_HEX32(0x00FF00u, canvas.getPixel(0, 0));
  TEST_ASSERT_EQUAL_HEX32(0xFF0000u, canvas.getPixel(16, 0));
  frame(200);
  TEST_ASSERT_EQUAL_HEX32(0xFF0000u, canvas.getPixel(0, 0));
  TEST_ASSERT_EQUAL_HEX32(0x00FF00u, canvas.getPixel(16, 0));
}

void test_static_page_icon_reuses_cached_pixels_and_clips_in_both_axes() {
  DevicePageIcon icon;
  s_trackPixels = true;
  const bool good = icon.begin("still", 51, 16) == awtrix::IconLoad::kGood;
  s_trackPixels = false;
  TEST_ASSERT_TRUE(good);
  // Decode into the one resident frame, which the page then takes ownership of.
  TEST_ASSERT_EQUAL_INT(1, s_pixelAllocations);
  Canvas canvas(51, 16);
  icon.advance(0);
  icon.blit(canvas, -4, -4);
  TEST_ASSERT_EQUAL_HEX32(0xFF0000u, canvas.getPixel(0, 0));
  TEST_ASSERT_EQUAL_HEX32(0xFF0000u, canvas.getPixel(3, 3));
  TEST_ASSERT_EQUAL_HEX32(0u, canvas.getPixel(4, 4));
  icon.advance(1000);
  icon.blit(canvas, 43, 8);
  TEST_ASSERT_EQUAL_HEX32(0xFF0000u, canvas.getPixel(50, 15));
}

void test_page_icon_tells_a_missing_image_from_an_out_of_memory_one() {
  DevicePageIcon icon;
  TEST_ASSERT_TRUE(icon.begin("absent", 32, 8) == awtrix::IconLoad::kMissing);
  s_jpgOom = true;
  TEST_ASSERT_TRUE(icon.begin("absent", 32, 8) == awtrix::IconLoad::kOom);
  TEST_ASSERT_EQUAL_INT(0, icon.width());
}

void test_inline_gif_page_icon_works_at_an_absolute_position() {
  std::string encoded(encode_base64_length(s_fast.size()), '\0');
  encode_base64(s_fast.data(), s_fast.size(), reinterpret_cast<unsigned char*>(&encoded[0]));
  DevicePageIcon icon;
  TEST_ASSERT_TRUE(icon.begin("data:image/gif;base64," + encoded, 51, 16) ==
                   awtrix::IconLoad::kGood);
  Canvas canvas(51, 16);
  icon.advance(0);
  icon.blit(canvas, 20, 8);
  TEST_ASSERT_EQUAL_HEX32(0xFF0000u, canvas.getPixel(27, 15));
  icon.advance(100);
  icon.blit(canvas, 20, 8);
  TEST_ASSERT_EQUAL_HEX32(0x00FF00u, canvas.getPixel(27, 15));
}

namespace {
const char* kCover = "https://example.com/cover.jpg?token=1";
using awtrix::media::RemoteState;
}

// Local icon loaders report URLs as missing without file access.
void test_url_icon_without_a_picture_source_is_missing_without_looking_for_files() {
  DevicePageIcon icon;
  TEST_ASSERT_TRUE(icon.begin(kCover, 52, 16) == awtrix::IconLoad::kMissing);
  TEST_ASSERT_TRUE(icon.beginNative(kCover, 20, 10) == awtrix::IconLoad::kMissing);
  TEST_ASSERT_EQUAL_INT(0, icon.width());
  int w = 0, h = 0;
  std::size_t bytes = 0;
  TEST_ASSERT_FALSE(icon.nativeInfo(kCover, 20, 10, w, h, bytes));
  TEST_ASSERT_EQUAL_INT(0, s_assetReads);
  TEST_ASSERT_EQUAL_INT(0, s_jpgDraws);
}

void test_url_icon_holds_its_square_while_pending_and_shows_once_settled() {
  FakeRemoteImages remote;
  RemotePageIcon icon(&remote);
  TEST_ASSERT_TRUE(icon.begin(kCover, 52, 16) == awtrix::IconLoad::kGood);
  TEST_ASSERT_EQUAL_STRING(kCover, remote.url.c_str());
  TEST_ASSERT_EQUAL_INT(16, remote.width);
  TEST_ASSERT_EQUAL_INT(16, remote.height);
  TEST_ASSERT_EQUAL_INT(16, icon.width());
  TEST_ASSERT_EQUAL_INT(16, icon.height());
  Canvas canvas(52, 16);
  icon.advance(0);
  icon.advance(40);
  icon.blit(canvas, 0, 0);
  TEST_ASSERT_EQUAL_HEX32(0u, canvas.getPixel(0, 0));
  TEST_ASSERT_EQUAL_INT(1, remote.asks);

  remote.still(16, 16, 0x123456u);
  remote.settle(RemoteState::kReady);
  icon.advance(80);
  TEST_ASSERT_EQUAL_INT(2, remote.asks);
  icon.blit(canvas, 0, 0);
  TEST_ASSERT_EQUAL_HEX32(0x123456u, canvas.getPixel(0, 0));
  TEST_ASSERT_EQUAL_HEX32(0x123456u, canvas.getPixel(15, 15));
  TEST_ASSERT_EQUAL_HEX32(0u, canvas.getPixel(16, 0));
  remote.settle(RemoteState::kReady);
  icon.advance(120);
  TEST_ASSERT_EQUAL_INT(2, remote.asks);
}

void test_failed_url_icon_keeps_its_square_empty_until_the_source_moves_on() {
  FakeRemoteImages remote;
  remote.state = RemoteState::kFailed;
  RemotePageIcon icon(&remote);
  TEST_ASSERT_TRUE(icon.begin(kCover, 32, 8) == awtrix::IconLoad::kGood);
  TEST_ASSERT_EQUAL_INT(8, icon.width());
  icon.advance(0);
  TEST_ASSERT_EQUAL_INT(1, remote.asks);
  Canvas canvas(32, 8);
  icon.blit(canvas, 0, 0);
  TEST_ASSERT_EQUAL_HEX32(0u, canvas.getPixel(0, 0));
  remote.still(8, 8, 0x00FF00u);
  remote.settle(RemoteState::kReady);
  icon.advance(40);
  TEST_ASSERT_EQUAL_INT(2, remote.asks);
  icon.blit(canvas, 0, 0);
  TEST_ASSERT_EQUAL_HEX32(0x00FF00u, canvas.getPixel(0, 0));
}

void test_url_gif_smaller_than_its_square_plays_in_the_middle() {
  FakeRemoteImages remote;
  remote.gif(sizedGif(8, 8), 8, 8);
  remote.state = RemoteState::kReady;
  RemotePageIcon icon(&remote);
  TEST_ASSERT_TRUE(icon.begin(kCover, 52, 16) == awtrix::IconLoad::kGood);
  TEST_ASSERT_EQUAL_INT(16, icon.width());
  Canvas canvas(52, 16);
  icon.advance(0);
  icon.blit(canvas, 0, 0);
  TEST_ASSERT_EQUAL_HEX32(0u, canvas.getPixel(3, 3));
  TEST_ASSERT_EQUAL_HEX32(0xFF0000u, canvas.getPixel(4, 4));
  TEST_ASSERT_EQUAL_HEX32(0xFF0000u, canvas.getPixel(11, 11));
  TEST_ASSERT_EQUAL_HEX32(0u, canvas.getPixel(12, 12));
  icon.advance(100);
  icon.blit(canvas, 0, 0);
  TEST_ASSERT_EQUAL_HEX32(0x00FF00u, canvas.getPixel(4, 4));
}

void test_layout_url_picture_fills_the_bounds_it_is_given() {
  FakeRemoteImages remote;
  RemotePageIcon factory(&remote);
  int w = 0, h = 0;
  std::size_t bytes = 0;
  TEST_ASSERT_TRUE(factory.nativeInfo(kCover, 20, 10, w, h, bytes));
  TEST_ASSERT_EQUAL_INT(20, w);
  TEST_ASSERT_EQUAL_INT(10, h);
  TEST_ASSERT_TRUE(bytes >= static_cast<std::size_t>(w * h) * sizeof(uint32_t));
  auto region = factory.create();
  TEST_ASSERT_TRUE(region->beginNativeBounded(kCover, 20, 10, bytes) == awtrix::IconLoad::kGood);
  TEST_ASSERT_EQUAL_INT(20, remote.width);
  TEST_ASSERT_EQUAL_INT(10, remote.height);
  TEST_ASSERT_EQUAL_INT(20, region->width());
  TEST_ASSERT_EQUAL_INT(10, region->height());
  TEST_ASSERT_TRUE(region->beginNativeBounded(kCover, 20, 10, bytes - 1) == awtrix::IconLoad::kMissing);
  TEST_ASSERT_EQUAL_INT(0, region->width());
}

void test_replacing_a_pending_url_with_a_local_icon_stops_remote_updates() {
  FakeRemoteImages remote;
  RemotePageIcon icon(&remote);
  TEST_ASSERT_TRUE(icon.begin(kCover, 32, 8) == awtrix::IconLoad::kGood);
  TEST_ASSERT_EQUAL_INT(1, remote.asks);
  TEST_ASSERT_TRUE(icon.begin("fast", 32, 8) == awtrix::IconLoad::kGood);
  remote.still(8, 8, 0x123456u);
  remote.settle(RemoteState::kReady);
  icon.advance(0);
  TEST_ASSERT_EQUAL_INT(1, remote.asks);
  Canvas canvas(32, 8);
  icon.blit(canvas, 0, 0);
  TEST_ASSERT_EQUAL_HEX32(0xFF0000u, canvas.getPixel(0, 0));
}

int main(int, char**) {
  UNITY_BEGIN();
  RUN_TEST(test_replacing_a_pending_url_with_a_local_icon_stops_remote_updates);
  RUN_TEST(test_multiple_page_icons_keep_independent_animation_schedules);
  RUN_TEST(test_static_page_icon_reuses_cached_pixels_and_clips_in_both_axes);
  RUN_TEST(test_inline_gif_page_icon_works_at_an_absolute_position);
  RUN_TEST(test_page_icon_tells_a_missing_image_from_an_out_of_memory_one);
  RUN_TEST(test_url_icon_without_a_picture_source_is_missing_without_looking_for_files);
  RUN_TEST(test_url_icon_holds_its_square_while_pending_and_shows_once_settled);
  RUN_TEST(test_failed_url_icon_keeps_its_square_empty_until_the_source_moves_on);
  RUN_TEST(test_url_gif_smaller_than_its_square_plays_in_the_middle);
  RUN_TEST(test_layout_url_picture_fills_the_bounds_it_is_given);
  return UNITY_END();
}
