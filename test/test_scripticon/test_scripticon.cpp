
#include <unity.h>

#include <cstdlib>
#include <cstring>
#include <new>
#include <string>

#include "core/render/Canvas.h"

namespace {
bool s_failBigAllocs = false;
constexpr std::size_t kFailThreshold = 10000;

bool shouldFail(std::size_t n) { return s_failBigAllocs && n >= kFailThreshold; }
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
#include "../../src/media/ScriptIcon.cpp"

#include <base64.hpp>

#include "../test_gifplayer/gif_fixtures.h"

namespace {
const unsigned char* s_asset = nullptr;
unsigned int s_assetLen = 0;
int s_readAssetCalls = 0;

void useAsset(const unsigned char* data, unsigned int len) {
  s_asset = data;
  s_assetLen = len;
}
}

namespace awtrix {
namespace media {
bool readAsset(const std::string& path, PodBuffer<uint8_t>& out) {
  (void)path;
  ++s_readAssetCalls;
  if (!s_asset) return false;
  if (!out.resize(s_assetLen)) return false;
  std::memcpy(out.data(), s_asset, s_assetLen);
  return true;
}
}

namespace icon {
bool draw(Canvas&, const std::string&, int, int) { return false; }
}
}

void setUp() {
  s_failBigAllocs = false;
  s_readAssetCalls = 0;
  useAsset(nullptr, 0);
}
void tearDown() { s_failBigAllocs = false; }

using awtrix::Canvas;
using awtrix::ScriptIcon;

namespace {

void assertColorNear(uint32_t expected, uint32_t actual) {
  TEST_ASSERT_UINT_WITHIN(8, (expected >> 16) & 0xFF, (actual >> 16) & 0xFF);
  TEST_ASSERT_UINT_WITHIN(8, (expected >> 8) & 0xFF, (actual >> 8) & 0xFF);
  TEST_ASSERT_UINT_WITHIN(8, expected & 0xFF, actual & 0xFF);
}

}

void test_good_icon_draws_and_caches() {
  useAsset(kGif8x8TwoFrames, kGif8x8TwoFrames_len);
  ScriptIcon si;
  Canvas c(32, 8);
  TEST_ASSERT_TRUE(si.draw(c, "a", 0, 0, 0));
  assertColorNear(0xFF0000u, c.getPixel(0, 0));
  const int reads = s_readAssetCalls;
  TEST_ASSERT_TRUE(si.draw(c, "a", 0, 0, 16));
  TEST_ASSERT_EQUAL_INT(reads, s_readAssetCalls);
}

void test_missing_icon_cached_without_retry() {
  useAsset(nullptr, 0);
  ScriptIcon si;
  Canvas c(32, 8);
  TEST_ASSERT_FALSE(si.draw(c, "nope", 0, 0, 0));
  const int reads = s_readAssetCalls;
  TEST_ASSERT_FALSE(si.draw(c, "nope", 0, 0, 60000));
  TEST_ASSERT_EQUAL_INT(reads, s_readAssetCalls);
  useAsset(kGif8x8TwoFrames, kGif8x8TwoFrames_len);
  TEST_ASSERT_FALSE(si.draw(c, "nope", 0, 0, 120000));
  TEST_ASSERT_EQUAL_INT(reads, s_readAssetCalls);
  si.invalidate();
  TEST_ASSERT_TRUE(si.draw(c, "nope", 0, 0, 180000));
}

void test_streaming_icon_renders_under_alloc_pressure() {
  useAsset(kGif32x8ManyFrames, kGif32x8ManyFrames_len);
  ScriptIcon si;
  Canvas c(32, 8);

  s_failBigAllocs = true;
  TEST_ASSERT_TRUE(si.draw(c, "big", 0, 0, 0));
  TEST_ASSERT_TRUE(si.draw(c, "big", 0, 0, 50));
  s_failBigAllocs = false;
}

void test_unsafe_names_rejected() {
  useAsset(kGif8x8TwoFrames, kGif8x8TwoFrames_len);
  ScriptIcon si;
  Canvas c(32, 8);
  TEST_ASSERT_FALSE(si.draw(c, "../secret", 0, 0, 0));
  TEST_ASSERT_FALSE(si.draw(c, "a/b", 0, 0, 0));
  TEST_ASSERT_FALSE(si.draw(c, "", 0, 0, 0));
  TEST_ASSERT_EQUAL_INT(0, s_readAssetCalls);
}

int main(int, char**) {
  UNITY_BEGIN();
  RUN_TEST(test_good_icon_draws_and_caches);
  RUN_TEST(test_missing_icon_cached_without_retry);
  RUN_TEST(test_streaming_icon_renders_under_alloc_pressure);
  RUN_TEST(test_unsafe_names_rejected);
  return UNITY_END();
}
