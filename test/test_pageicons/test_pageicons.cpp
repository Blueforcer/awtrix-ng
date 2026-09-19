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

#include <base64.hpp>

#include "../test_gifplayer/sized_gif_fixture.h"

namespace {
std::vector<uint8_t> s_fast, s_slow, s_still;
bool s_jpgOom = false;
}

namespace awtrix {
namespace media {
bool readAsset(const std::string& path, PodBuffer<uint8_t>& out, bool* outOfMemory) {
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
bool draw(Canvas&, const std::string&, int, int, bool* outOfMemory) {
  if (outOfMemory) *outOfMemory = s_jpgOom;
  return false;
}
}
}

using awtrix::Canvas;
using awtrix::DevicePageIcon;

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
  // One scratch canvas and one resident frame: the page takes ownership of that frame.
  TEST_ASSERT_EQUAL_INT(2, s_pixelAllocations);
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
  TEST_ASSERT_TRUE(icon.begin(encoded, 51, 16) == awtrix::IconLoad::kGood);
  Canvas canvas(51, 16);
  icon.advance(0);
  icon.blit(canvas, 20, 8);
  TEST_ASSERT_EQUAL_HEX32(0xFF0000u, canvas.getPixel(27, 15));
  icon.advance(100);
  icon.blit(canvas, 20, 8);
  TEST_ASSERT_EQUAL_HEX32(0x00FF00u, canvas.getPixel(27, 15));
}

int main(int, char**) {
  UNITY_BEGIN();
  RUN_TEST(test_multiple_page_icons_keep_independent_animation_schedules);
  RUN_TEST(test_static_page_icon_reuses_cached_pixels_and_clips_in_both_axes);
  RUN_TEST(test_inline_gif_page_icon_works_at_an_absolute_position);
  RUN_TEST(test_page_icon_tells_a_missing_image_from_an_out_of_memory_one);
  return UNITY_END();
}
