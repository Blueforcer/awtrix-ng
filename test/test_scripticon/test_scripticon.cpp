
#include <unity.h>

#include <cstddef>
#include <cstdlib>
#include <cstring>
#include <array>
#include <new>
#include <string>

#include "core/render/Canvas.h"

namespace {
bool s_failBigAllocs = false;
constexpr std::size_t kFailThreshold = 10000;
bool s_trackAllocations = false;
std::size_t s_allocatedBytes = 0;
std::size_t s_liveBytes = 0;
int s_failNothrowNth = 0;

struct alignas(std::max_align_t) AllocHeader {
  std::size_t bytes;
};

bool shouldFail(std::size_t n) {
  if (s_trackAllocations) s_allocatedBytes += n;
  return s_failBigAllocs && n >= kFailThreshold;
}

void* allocate(std::size_t n) {
  auto* h = static_cast<AllocHeader*>(std::malloc(sizeof(AllocHeader) + n));
  if (!h) return nullptr;
  h->bytes = n;
  s_liveBytes += n;
  return h + 1;
}

void deallocate(void* p) {
  if (!p) return;
  auto* h = static_cast<AllocHeader*>(p) - 1;
  s_liveBytes -= h->bytes;
  std::free(h);
}
}

void* operator new(std::size_t n) {
  if (!shouldFail(n)) {
    if (void* p = allocate(n)) return p;
  }
  throw std::bad_alloc();
}
void* operator new(std::size_t n, const std::nothrow_t&) noexcept {
  if (s_failNothrowNth > 0 && --s_failNothrowNth == 0) return nullptr;
  if (shouldFail(n)) return nullptr;
  return allocate(n);
}
void* operator new[](std::size_t n) { return operator new(n); }
void* operator new[](std::size_t n, const std::nothrow_t&) noexcept {
  return operator new(n, std::nothrow);
}
void operator delete(void* p) noexcept { deallocate(p); }
void operator delete(void* p, std::size_t) noexcept { deallocate(p); }
void operator delete(void* p, const std::nothrow_t&) noexcept { deallocate(p); }
void operator delete[](void* p) noexcept { deallocate(p); }
void operator delete[](void* p, std::size_t) noexcept { deallocate(p); }
void operator delete[](void* p, const std::nothrow_t&) noexcept { deallocate(p); }

#include "../../src/media/GifPlayer.cpp"
#include "../../src/media/MicroGif.cpp"
#include "../../src/media/ScriptIcon.cpp"
#include "media/DevicePageIcon.h"

#include <base64.hpp>

#include "../test_gifplayer/gif_fixtures.h"
#include "../test_gifplayer/sized_gif_fixture.h"

namespace {
const unsigned char* s_asset = nullptr;
unsigned int s_assetLen = 0;
int s_readAssetCalls = 0;
bool s_assetOom = false;
bool s_jpgDraws = false;
bool s_jpgOom = false;
int s_jpgCalls = 0;

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
  ++s_readAssetCalls;
  if (s_assetOom) {
    if (outOfMemory) *outOfMemory = true;
    return false;
  }
  if (!s_asset) return false;
  if (!out.resize(s_assetLen)) {
    if (outOfMemory) *outOfMemory = true;
    return false;
  }
  std::memcpy(out.data(), s_asset, s_assetLen);
  return true;
}
}

namespace icon {
bool draw(Canvas& canvas, const std::string&, int x, int y, bool* outOfMemory) {
  ++s_jpgCalls;
  if (outOfMemory) *outOfMemory = s_jpgOom;
  if (s_jpgOom || !s_jpgDraws) return false;
  canvas.fillRect(x, y, 8, 8, 0x00AA00u);
  return true;
}
}
}

void setUp() {
  s_failBigAllocs = false;
  s_failNothrowNth = 0;
  s_readAssetCalls = 0;
  s_assetOom = false;
  s_jpgDraws = false;
  s_jpgOom = false;
  s_jpgCalls = 0;
  useAsset(nullptr, 0);
}
void tearDown() {
  s_failBigAllocs = false;
  s_failNothrowNth = 0;
  s_trackAllocations = false;
  s_assetOom = false;
}

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
  auto set = si.createSet();
  Canvas c(32, 8);
  si.setPanelSize(c.width(), c.height());
  TEST_ASSERT_TRUE(set->draw(c, "a", 0, 0, 0));
  assertColorNear(0xFF0000u, c.getPixel(0, 0));
  const int reads = s_readAssetCalls;
  TEST_ASSERT_TRUE(set->draw(c, "a", 0, 0, 16));
  TEST_ASSERT_EQUAL_INT(reads, s_readAssetCalls);
}

void test_missing_icon_cached_without_retry() {
  useAsset(nullptr, 0);
  ScriptIcon si;
  auto set = si.createSet();
  Canvas c(32, 8);
  si.setPanelSize(c.width(), c.height());
  TEST_ASSERT_FALSE(set->draw(c, "nope", 0, 0, 0));
  const int reads = s_readAssetCalls;
  TEST_ASSERT_FALSE(set->draw(c, "nope", 0, 0, 60000));
  TEST_ASSERT_EQUAL_INT(reads, s_readAssetCalls);
  useAsset(kGif8x8TwoFrames, kGif8x8TwoFrames_len);
  TEST_ASSERT_FALSE(set->draw(c, "nope", 0, 0, 120000));
  TEST_ASSERT_EQUAL_INT(reads, s_readAssetCalls);
  si.invalidate();
  TEST_ASSERT_TRUE(set->draw(c, "nope", 0, 0, 180000));
}

void test_streaming_icon_renders_under_alloc_pressure() {
  useAsset(kGif32x8ManyFrames, kGif32x8ManyFrames_len);
  ScriptIcon si;
  auto set = si.createSet();
  Canvas c(32, 8);
  si.setPanelSize(c.width(), c.height());

  s_failBigAllocs = true;
  TEST_ASSERT_TRUE(set->draw(c, "big", 0, 0, 0));
  TEST_ASSERT_EQUAL_HEX32(0xFF0000u, c.getPixel(16, 4));
  TEST_ASSERT_TRUE(set->draw(c, "big", 0, 0, 50));
  TEST_ASSERT_EQUAL_HEX32(0x0000FFu, c.getPixel(16, 4));
  s_failBigAllocs = false;
}

void test_wide_icon_draws_full_width() {
  useAsset(kGif32x8TwoFrames, kGif32x8TwoFrames_len);
  ScriptIcon si;
  auto set = si.createSet();
  Canvas c(32, 8);
  si.setPanelSize(c.width(), c.height());
  TEST_ASSERT_TRUE(set->draw(c, "w", 0, 0, 0));
  TEST_ASSERT_EQUAL_HEX32(0x0000FFu, c.getPixel(0, 0));
  TEST_ASSERT_EQUAL_HEX32(0x0000FFu, c.getPixel(31, 7));
}

void test_icon_draws_only_its_own_size() {
  useAsset(kGif8x8TwoFrames, kGif8x8TwoFrames_len);
  ScriptIcon si;
  auto set = si.createSet();
  Canvas c(32, 8);
  si.setPanelSize(c.width(), c.height());
  c.clear(0x123456u);
  TEST_ASSERT_TRUE(set->draw(c, "s", 0, 0, 0));
  assertColorNear(0xFF0000u, c.getPixel(0, 0));
  TEST_ASSERT_EQUAL_HEX32(0x123456u, c.getPixel(8, 0));
  TEST_ASSERT_EQUAL_HEX32(0x123456u, c.getPixel(31, 7));
}

void test_mixed_sizes_coexist_in_cache() {
  ScriptIcon si;
  auto set = si.createSet();
  Canvas c(32, 8);
  si.setPanelSize(c.width(), c.height());
  useAsset(kGif32x8TwoFrames, kGif32x8TwoFrames_len);
  TEST_ASSERT_TRUE(set->draw(c, "wide", 0, 0, 0));
  TEST_ASSERT_EQUAL_HEX32(0x0000FFu, c.getPixel(31, 7));

  c.clear(0x123456u);
  useAsset(kGif8x8TwoFrames, kGif8x8TwoFrames_len);
  TEST_ASSERT_TRUE(set->draw(c, "small", 0, 0, 0));
  assertColorNear(0xFF0000u, c.getPixel(0, 0));
  TEST_ASSERT_EQUAL_HEX32(0x123456u, c.getPixel(8, 0));

  const int reads = s_readAssetCalls;
  c.clear(0x000000u);
  TEST_ASSERT_TRUE(set->draw(c, "wide", 0, 0, 0));
  TEST_ASSERT_EQUAL_INT(reads, s_readAssetCalls);
  TEST_ASSERT_EQUAL_HEX32(0x0000FFu, c.getPixel(31, 7));
}

void test_offset_is_honoured() {
  useAsset(kGif32x8TwoFrames, kGif32x8TwoFrames_len);
  ScriptIcon si;
  auto set = si.createSet();
  Canvas c(32, 8);
  si.setPanelSize(c.width(), c.height());
  c.clear(0x123456u);
  TEST_ASSERT_TRUE(set->draw(c, "w", 4, 0, 0));
  TEST_ASSERT_EQUAL_HEX32(0x123456u, c.getPixel(3, 0));
  TEST_ASSERT_EQUAL_HEX32(0x0000FFu, c.getPixel(4, 0));
  TEST_ASSERT_EQUAL_HEX32(0x0000FFu, c.getPixel(31, 7));
}

void test_invalidate_reloads_wide_icon() {
  useAsset(kGif32x8TwoFrames, kGif32x8TwoFrames_len);
  ScriptIcon si;
  auto set = si.createSet();
  Canvas c(32, 8);
  si.setPanelSize(c.width(), c.height());
  TEST_ASSERT_TRUE(set->draw(c, "w", 0, 0, 0));
  const int reads = s_readAssetCalls;
  si.invalidate();
  c.clear(0x000000u);
  TEST_ASSERT_TRUE(set->draw(c, "w", 0, 0, 0));
  TEST_ASSERT_EQUAL_INT(reads + 1, s_readAssetCalls);
  TEST_ASSERT_EQUAL_HEX32(0x0000FFu, c.getPixel(31, 7));
}

void test_jpg_fallback_out_of_memory_is_retried_not_written_off() {
  ScriptIcon si;
  auto set = si.createSet();
  Canvas c(32, 8);
  si.setPanelSize(c.width(), c.height());
  s_jpgOom = true;
  TEST_ASSERT_FALSE(set->draw(c, "jpgonly", 0, 0, 0));
  TEST_ASSERT_EQUAL_INT(1, s_jpgCalls);
  TEST_ASSERT_FALSE(set->draw(c, "jpgonly", 0, 0, 1000));
  TEST_ASSERT_EQUAL_INT(1, s_jpgCalls);
  s_jpgOom = false;
  s_jpgDraws = true;
  TEST_ASSERT_TRUE(set->draw(c, "jpgonly", 0, 0, 2000));
  TEST_ASSERT_EQUAL_INT(2, s_jpgCalls);
  TEST_ASSERT_EQUAL_HEX32(0x00AA00u, c.getPixel(7, 7));
}

void test_missing_jpg_stays_missing_until_invalidated() {
  ScriptIcon si;
  auto set = si.createSet();
  Canvas c(32, 8);
  si.setPanelSize(c.width(), c.height());
  TEST_ASSERT_FALSE(set->draw(c, "nothing", 0, 0, 0));
  TEST_ASSERT_FALSE(set->draw(c, "nothing", 0, 0, 60000));
  TEST_ASSERT_EQUAL_INT(1, s_jpgCalls);
  s_jpgDraws = true;
  si.invalidate();
  TEST_ASSERT_TRUE(set->draw(c, "nothing", 0, 0, 60001));
}

void test_extreme_coordinates_draw_nothing_and_do_not_overflow() {
  useAsset(kGif8x8TwoFrames, kGif8x8TwoFrames_len);
  ScriptIcon si;
  auto set = si.createSet();
  Canvas c(32, 8);
  si.setPanelSize(c.width(), c.height());
  c.clear(0x123456u);
  TEST_ASSERT_TRUE(set->draw(c, "a", 2147483647, 2147483647, 0));
  TEST_ASSERT_TRUE(set->draw(c, "a", -2147483647 - 1, -2147483647 - 1, 0));
  for (int y = 0; y < 8; ++y)
    for (int x = 0; x < 32; ++x) TEST_ASSERT_EQUAL_HEX32(0x123456u, c.getPixel(x, y));
}

void test_unsafe_names_rejected() {
  useAsset(kGif8x8TwoFrames, kGif8x8TwoFrames_len);
  ScriptIcon si;
  auto set = si.createSet();
  Canvas c(32, 8);
  si.setPanelSize(c.width(), c.height());
  TEST_ASSERT_FALSE(set->draw(c, "../secret", 0, 0, 0));
  TEST_ASSERT_FALSE(set->draw(c, "a/b", 0, 0, 0));
  TEST_ASSERT_FALSE(set->draw(c, "", 0, 0, 0));
  TEST_ASSERT_EQUAL_INT(0, s_readAssetCalls);
}

void test_panel_sized_script_icon_draws_all_rows_and_columns() {
  const auto asset = sizedGif(51, 16);
  useAsset(asset.data(), asset.size());
  ScriptIcon si;
  auto set = si.createSet();
  Canvas c(51, 16);
  si.setPanelSize(c.width(), c.height());
  TEST_ASSERT_TRUE(set->draw(c, "large", 0, 0, 0));
  TEST_ASSERT_EQUAL_HEX32(0xFF0000u, c.getPixel(50, 15));
  TEST_ASSERT_TRUE(set->draw(c, "large", 0, 0, 100));
  TEST_ASSERT_EQUAL_HEX32(0x00FF00u, c.getPixel(50, 15));
}

void test_script_icon_reloads_when_panel_bounds_change() {
  const auto asset = sizedGif(51, 16);
  useAsset(asset.data(), asset.size());
  ScriptIcon si;
  auto set = si.createSet();
  Canvas narrow(41, 8);
  Canvas wide(51, 16);
  si.setPanelSize(41, 8);
  TEST_ASSERT_FALSE(set->draw(narrow, "large", 0, 0, 0));
  si.setPanelSize(51, 16);
  TEST_ASSERT_TRUE(set->draw(wide, "large", 0, 0, 0));
  TEST_ASSERT_EQUAL_HEX32(0xFF0000u, wide.getPixel(50, 15));
  si.setPanelSize(41, 8);
  TEST_ASSERT_FALSE(set->draw(narrow, "large", 0, 0, 0));
}

void test_destination_size_does_not_restart_script_animation() {
  const auto asset = sizedGif(51, 16);
  useAsset(asset.data(), asset.size());
  ScriptIcon si;
  auto set = si.createSet();
  si.setPanelSize(51, 16);
  Canvas narrow(8, 8);
  Canvas wide(51, 16);
  TEST_ASSERT_TRUE(set->draw(narrow, "a", 0, 0, 0));
  TEST_ASSERT_TRUE(set->draw(wide, "a", 0, 0, 100));
  TEST_ASSERT_EQUAL_HEX32(0x00FF00u, wide.getPixel(50, 15));
  si.setPanelSize(51, 16);
  TEST_ASSERT_TRUE(set->draw(narrow, "a", 0, 0, 150));
  TEST_ASSERT_EQUAL_HEX32(0x00FF00u, narrow.getPixel(7, 7));
  TEST_ASSERT_EQUAL_INT(1, s_readAssetCalls);
}

void test_script_icons_require_configured_panel_bounds() {
  ScriptIcon si;
  auto set = si.createSet();
  Canvas c(32, 8);
  TEST_ASSERT_FALSE(set->draw(c, "a", 0, 0, 0));
  TEST_ASSERT_EQUAL_INT(0, s_readAssetCalls);
}

void test_small_script_icon_uses_same_ram_on_larger_panels() {
  useAsset(kGif8x8TwoFrames, kGif8x8TwoFrames_len);
  std::size_t baseline = 0;
  for (int panelSize : {8, 51, 1024}) {
    ScriptIcon si;
    auto set = si.createSet();
    Canvas c(panelSize, panelSize);
    si.setPanelSize(c.width(), c.height());
    s_allocatedBytes = 0;
    s_trackAllocations = true;
    const bool good = set->draw(c, "small", 0, 0, 0);
    s_trackAllocations = false;
    TEST_ASSERT_TRUE(good);
    if (panelSize == 8) baseline = s_allocatedBytes;
    else TEST_ASSERT_EQUAL_UINT(baseline, s_allocatedBytes);
  }
}

void test_page_icon_renders_panel_sized_animation_and_releases_on_clear() {
  const auto asset = sizedGif(51, 16);
  useAsset(asset.data(), asset.size());
  awtrix::DevicePageIcon icon;
  TEST_ASSERT_TRUE(icon.begin("large", 51, 16) == awtrix::IconLoad::kGood);
  TEST_ASSERT_EQUAL_INT(51, icon.width());
  Canvas c(51, 16);
  icon.advance(0);
  icon.blit(c, 0);
  TEST_ASSERT_EQUAL_HEX32(0xFF0000u, c.getPixel(50, 15));
  icon.advance(100);
  icon.blit(c, 0);
  TEST_ASSERT_EQUAL_HEX32(0x00FF00u, c.getPixel(50, 15));
  icon.clear();
  TEST_ASSERT_EQUAL_INT(0, icon.width());
  TEST_ASSERT_TRUE(icon.begin("large", 41, 8) == awtrix::IconLoad::kMissing);
}

void test_small_page_icon_uses_same_ram_on_larger_panels() {
  useAsset(kGif8x8TwoFrames, kGif8x8TwoFrames_len);
  std::size_t baseline = 0;
  for (int panelSize : {8, 51, 1024}) {
    awtrix::DevicePageIcon icon;
    s_allocatedBytes = 0;
    s_trackAllocations = true;
    const bool good = icon.begin("small", panelSize, panelSize) == awtrix::IconLoad::kGood;
    s_trackAllocations = false;
    TEST_ASSERT_TRUE(good);
    TEST_ASSERT_EQUAL_INT(8, icon.width());
    if (panelSize == 8) baseline = s_allocatedBytes;
    else TEST_ASSERT_EQUAL_UINT(baseline, s_allocatedBytes);
  }
}

void test_four_icons_keep_independent_animation_without_reloading() {
  std::array<std::vector<uint8_t>, 4> assets;
  const std::array<std::string, 4> names = {"a", "b", "c", "d"};
  ScriptIcon si;
  auto set = si.createSet();
  Canvas c(41, 8);
  si.setPanelSize(c.width(), c.height());
  for (size_t i = 0; i < assets.size(); ++i) {
    assets[i] = sizedGif(8, 8);
    // Give each synthetic GIF its own 100/200/300/400 ms frame interval.
    for (size_t offset = 0; offset + 7 < assets[i].size(); ++offset) {
      if (assets[i][offset] == 0x21 && assets[i][offset + 1] == 0xf9 &&
          assets[i][offset + 2] == 4 && assets[i][offset + 3] == 4) {
        assets[i][offset + 4] = static_cast<uint8_t>((i + 1) * 10);
      }
    }
    useAsset(assets[i].data(), assets[i].size());
    TEST_ASSERT_TRUE(set->draw(c, names[i], static_cast<int>(i) * 8, 0, 0));
  }
  TEST_ASSERT_EQUAL_INT(4, s_readAssetCalls);
  useAsset(nullptr, 0);  // Any accidental eviction/reload now also fails to draw.
  for (int now = 100; now <= 1000; now += 100) {
    c.clear();
    for (size_t i = 0; i < names.size(); ++i) {
      TEST_ASSERT_TRUE(set->draw(c, names[i], static_cast<int>(i) * 8, 0, now));
      const uint32_t expected = (now / ((i + 1) * 100)) % 2 ? 0x00FF00u : 0xFF0000u;
      TEST_ASSERT_EQUAL_HEX32(expected, c.getPixel(static_cast<int>(i) * 8, 0));
      TEST_ASSERT_EQUAL_HEX32(expected, c.getPixel(static_cast<int>(i) * 8 + 7, 7));
    }
  }
  TEST_ASSERT_EQUAL_INT(4, s_readAssetCalls);
}

void test_duplicate_id_at_two_positions_shares_animation() {
  const auto asset = sizedGif(8, 8);
  useAsset(asset.data(), asset.size());
  ScriptIcon si;
  auto set = si.createSet();
  Canvas c(41, 8);
  si.setPanelSize(c.width(), c.height());
  for (int now : {0, 100, 200}) {
    c.clear();
    TEST_ASSERT_TRUE(set->draw(c, "shared", 0, 0, now));
    TEST_ASSERT_TRUE(set->draw(c, "shared", 24, 0, now));
    const uint32_t expected = now == 100 ? 0x00FF00u : 0xFF0000u;
    TEST_ASSERT_EQUAL_HEX32(expected, c.getPixel(0, 0));
    TEST_ASSERT_EQUAL_HEX32(expected, c.getPixel(31, 7));
    TEST_ASSERT_EQUAL_HEX32(0u, c.getPixel(8, 0));
  }
  TEST_ASSERT_EQUAL_INT(1, s_readAssetCalls);
}

void test_four_ids_stay_pinned_and_a_fifth_does_not_thrash() {
  const auto asset = sizedGif(8, 8);
  useAsset(asset.data(), asset.size());
  ScriptIcon si;
  auto set = si.createSet();
  Canvas c(41, 8);
  si.setPanelSize(c.width(), c.height());
  for (int now : {0, 100, 200}) {
    for (int id = 0; id < 4; ++id) {
      TEST_ASSERT_TRUE(set->draw(c, "id" + std::to_string(id), 0, 0, now));
      TEST_ASSERT_EQUAL_HEX32(now == 100 ? 0x00FF00u : 0xFF0000u, c.getPixel(0, 0));
    }
    TEST_ASSERT_FALSE(set->draw(c, "overflow", 0, 0, now));
  }
  TEST_ASSERT_EQUAL_INT(4, s_readAssetCalls);
  // On a later tick there is room to replace the oldest inactive entry.
  TEST_ASSERT_TRUE(set->draw(c, "overflow", 0, 0, 300));
  TEST_ASSERT_EQUAL_INT(5, s_readAssetCalls);
}

void test_unused_script_icon_cache_allocates_nothing() {
  s_allocatedBytes = 0;
  s_trackAllocations = true;
  {
    ScriptIcon si;
    si.invalidate();
  }
  s_trackAllocations = false;
  TEST_ASSERT_EQUAL_UINT(0, s_allocatedBytes);
}

void test_cache_entry_allocation_failure_is_retryable() {
  const auto asset = sizedGif(8, 8);
  useAsset(asset.data(), asset.size());
  ScriptIcon si;
  auto set = si.createSet();
  Canvas c(41, 8);
  si.setPanelSize(c.width(), c.height());
  s_failNothrowNth = 1;
  TEST_ASSERT_FALSE(set->draw(c, "a", 0, 0, 0));
  TEST_ASSERT_EQUAL_INT(0, s_readAssetCalls);
  TEST_ASSERT_TRUE(set->draw(c, "a", 0, 0, 1));
  TEST_ASSERT_EQUAL_INT(1, s_readAssetCalls);
}

void test_player_allocation_failure_honours_retry_backoff() {
  const auto asset = sizedGif(8, 8);
  useAsset(asset.data(), asset.size());
  ScriptIcon si;
  auto set = si.createSet();
  Canvas c(41, 8);
  si.setPanelSize(c.width(), c.height());
  s_failNothrowNth = 2;  // Entry succeeds; GifPlayer allocation fails.
  TEST_ASSERT_FALSE(set->draw(c, "a", 0, 0, 0));
  TEST_ASSERT_FALSE(set->draw(c, "a", 0, 0, 1999));
  TEST_ASSERT_EQUAL_INT(0, s_readAssetCalls);
  TEST_ASSERT_TRUE(set->draw(c, "a", 0, 0, 2000));
  TEST_ASSERT_EQUAL_INT(1, s_readAssetCalls);
}

void test_static_frame_survives_player_release_and_invalidation() {
  const auto asset = sizedGif(8, 8, false);
  useAsset(asset.data(), asset.size());
  ScriptIcon si;
  auto set = si.createSet();
  Canvas c(41, 8);
  si.setPanelSize(c.width(), c.height());
  TEST_ASSERT_TRUE(set->draw(c, "still", 0, 0, 0));
  useAsset(nullptr, 0);
  s_allocatedBytes = 0;
  s_trackAllocations = true;
  c.clear();
  const bool good = set->draw(c, "still", 24, 0, 50000);
  s_trackAllocations = false;
  TEST_ASSERT_TRUE(good);
  TEST_ASSERT_EQUAL_UINT(0, s_allocatedBytes);
  TEST_ASSERT_EQUAL_HEX32(0xFF0000u, c.getPixel(31, 7));
  TEST_ASSERT_EQUAL_INT(1, s_readAssetCalls);
  si.invalidate();
  TEST_ASSERT_FALSE(set->draw(c, "still", 0, 0, 50001));
}

void test_asset_read_oom_retries_instead_of_caching_missing_icon() {
  const auto asset = sizedGif(8, 8);
  useAsset(asset.data(), asset.size());
  ScriptIcon si;
  auto set = si.createSet();
  Canvas c(41, 8);
  si.setPanelSize(c.width(), c.height());
  s_assetOom = true;
  TEST_ASSERT_FALSE(set->draw(c, "a", 0, 0, 0));
  TEST_ASSERT_EQUAL_INT(1, s_readAssetCalls);
  s_assetOom = false;
  TEST_ASSERT_FALSE(set->draw(c, "a", 0, 0, 1999));
  TEST_ASSERT_EQUAL_INT(1, s_readAssetCalls);
  TEST_ASSERT_TRUE(set->draw(c, "a", 0, 0, 2000));
  TEST_ASSERT_EQUAL_INT(2, s_readAssetCalls);
  TEST_ASSERT_EQUAL_HEX32(0xFF0000u, c.getPixel(7, 7));
}

void test_release_frees_streamed_and_static_icons_and_reloads_once() {
  ScriptIcon si;
  auto set = si.createSet();
  Canvas c(41, 8);
  si.setPanelSize(c.width(), c.height());
  const auto animated = sizedGif(8, 8);
  const auto still = sizedGif(8, 8, false);
  TEST_ASSERT_FALSE(set->draw(c, "warmup", 0, 0, 0));
  set->release();
  s_readAssetCalls = 0;
  const std::size_t baseline = s_liveBytes;

  useAsset(animated.data(), animated.size());
  TEST_ASSERT_TRUE(set->draw(c, "anim", 0, 0, 0));
  useAsset(still.data(), still.size());
  TEST_ASSERT_TRUE(set->draw(c, "still", 8, 0, 0));
  TEST_ASSERT_TRUE(s_liveBytes > baseline);
  TEST_ASSERT_EQUAL_INT(2, s_readAssetCalls);

  set->release();
  TEST_ASSERT_EQUAL_UINT(baseline, s_liveBytes);

  useAsset(animated.data(), animated.size());
  TEST_ASSERT_TRUE(set->draw(c, "anim", 0, 0, 5000));
  TEST_ASSERT_TRUE(set->draw(c, "anim", 0, 0, 5001));
  TEST_ASSERT_EQUAL_INT(3, s_readAssetCalls);
  TEST_ASSERT_EQUAL_HEX32(0xFF0000u, c.getPixel(7, 7));
}

void test_release_on_empty_set_allocates_nothing() {
  ScriptIcon si;
  auto set = si.createSet();
  s_allocatedBytes = 0;
  s_trackAllocations = true;
  set->release();
  set->release();
  s_trackAllocations = false;
  TEST_ASSERT_EQUAL_UINT(0, s_allocatedBytes);
}

void test_destroying_a_set_frees_everything_it_held() {
  const auto asset = sizedGif(8, 8);
  useAsset(asset.data(), asset.size());
  ScriptIcon si;
  Canvas c(41, 8);
  si.setPanelSize(c.width(), c.height());
  {
    auto warmup = si.createSet();
    TEST_ASSERT_TRUE(warmup->draw(c, "a", 0, 0, 0));
  }
  const std::size_t baseline = s_liveBytes;
  {
    auto set = si.createSet();
    TEST_ASSERT_TRUE(set->draw(c, "a", 0, 0, 0));
    TEST_ASSERT_TRUE(set->draw(c, "b", 8, 0, 0));
    TEST_ASSERT_TRUE(s_liveBytes > baseline);
  }
  TEST_ASSERT_EQUAL_UINT(baseline, s_liveBytes);
}

void test_missing_icon_is_looked_up_once_per_showing() {
  useAsset(nullptr, 0);
  ScriptIcon si;
  auto set = si.createSet();
  Canvas c(32, 8);
  si.setPanelSize(c.width(), c.height());
  TEST_ASSERT_FALSE(set->draw(c, "nope", 0, 0, 0));
  TEST_ASSERT_FALSE(set->draw(c, "nope", 0, 0, 9000));
  TEST_ASSERT_EQUAL_INT(1, s_readAssetCalls);
  set->release();
  TEST_ASSERT_FALSE(set->draw(c, "nope", 0, 0, 10000));
  TEST_ASSERT_FALSE(set->draw(c, "nope", 0, 0, 19000));
  TEST_ASSERT_EQUAL_INT(2, s_readAssetCalls);
}

void test_release_drops_oom_backoff_and_log_is_rate_limited() {
  const auto asset = sizedGif(8, 8);
  useAsset(asset.data(), asset.size());
  ScriptIcon si;
  int logLines = 0;
  si.setLog([&](const std::string&) { ++logLines; });
  auto set = si.createSet();
  Canvas c(41, 8);
  si.setPanelSize(c.width(), c.height());

  s_failNothrowNth = 2;
  TEST_ASSERT_FALSE(set->draw(c, "a", 0, 0, 0));
  TEST_ASSERT_EQUAL_INT(1, logLines);
  set->release();
  TEST_ASSERT_TRUE(set->draw(c, "a", 0, 0, 10));
  TEST_ASSERT_EQUAL_INT(1, s_readAssetCalls);

  set->release();
  s_failNothrowNth = 2;
  TEST_ASSERT_FALSE(set->draw(c, "a", 0, 0, 20000));
  TEST_ASSERT_EQUAL_INT(1, logLines);
  set->release();
  s_failNothrowNth = 2;
  TEST_ASSERT_FALSE(set->draw(c, "a", 0, 0, 70000));
  TEST_ASSERT_EQUAL_INT(2, logLines);
}

void test_two_sets_hold_the_same_icon_independently() {
  const auto asset = sizedGif(8, 8);
  useAsset(asset.data(), asset.size());
  ScriptIcon si;
  auto first = si.createSet();
  auto second = si.createSet();
  Canvas a(8, 8);
  Canvas b(8, 8);
  si.setPanelSize(41, 8);
  TEST_ASSERT_TRUE(first->draw(a, "shared", 0, 0, 0));
  TEST_ASSERT_TRUE(second->draw(b, "shared", 0, 0, 50));
  TEST_ASSERT_EQUAL_INT(2, s_readAssetCalls);

  TEST_ASSERT_TRUE(first->draw(a, "shared", 0, 0, 100));
  TEST_ASSERT_TRUE(second->draw(b, "shared", 0, 0, 100));
  TEST_ASSERT_EQUAL_HEX32(0x00FF00u, a.getPixel(0, 0));
  TEST_ASSERT_EQUAL_HEX32(0xFF0000u, b.getPixel(0, 0));

  useAsset(nullptr, 0);
  first->release();
  TEST_ASSERT_TRUE(second->draw(b, "shared", 0, 0, 150));
  TEST_ASSERT_EQUAL_HEX32(0x00FF00u, b.getPixel(0, 0));
  TEST_ASSERT_EQUAL_INT(2, s_readAssetCalls);
  TEST_ASSERT_FALSE(first->draw(a, "shared", 0, 0, 150));
}

void test_invalidate_reaches_every_existing_set() {
  const auto asset = sizedGif(8, 8, false);
  useAsset(asset.data(), asset.size());
  ScriptIcon si;
  auto first = si.createSet();
  auto second = si.createSet();
  Canvas c(41, 8);
  si.setPanelSize(c.width(), c.height());
  TEST_ASSERT_TRUE(first->draw(c, "a", 0, 0, 0));
  TEST_ASSERT_TRUE(second->draw(c, "a", 0, 0, 0));
  TEST_ASSERT_EQUAL_INT(2, s_readAssetCalls);
  si.invalidate();
  TEST_ASSERT_TRUE(first->draw(c, "a", 0, 0, 1));
  TEST_ASSERT_TRUE(second->draw(c, "a", 0, 0, 1));
  TEST_ASSERT_TRUE(second->draw(c, "a", 0, 0, 2));
  TEST_ASSERT_EQUAL_INT(4, s_readAssetCalls);
}

void test_each_set_has_its_own_four_ids() {
  const auto asset = sizedGif(8, 8);
  useAsset(asset.data(), asset.size());
  ScriptIcon si;
  auto first = si.createSet();
  auto second = si.createSet();
  Canvas c(41, 8);
  si.setPanelSize(c.width(), c.height());
  for (int id = 0; id < 4; ++id)
    TEST_ASSERT_TRUE(first->draw(c, "id" + std::to_string(id), 0, 0, 0));
  TEST_ASSERT_FALSE(first->draw(c, "overflow", 0, 0, 0));
  TEST_ASSERT_TRUE(second->draw(c, "overflow", 0, 0, 0));
}

void test_create_set_allocation_failure_returns_null_and_recovers() {
  ScriptIcon si;
  s_failNothrowNth = 1;
  TEST_ASSERT_NULL(si.createSet().get());
  TEST_ASSERT_NOT_NULL(si.createSet().get());
}

void test_long_name_needs_no_allocation_once_cached() {
  const auto asset = sizedGif(8, 8, false);
  useAsset(asset.data(), asset.size());
  ScriptIcon si;
  auto set = si.createSet();
  Canvas c(41, 8);
  si.setPanelSize(c.width(), c.height());
  const char* name = "an-icon-name-well-past-the-small-string-buffer";
  TEST_ASSERT_TRUE(set->draw(c, name, 0, 0, 0));
  s_allocatedBytes = 0;
  s_trackAllocations = true;
  const bool good = set->draw(c, name, 0, 0, 1);
  s_trackAllocations = false;
  TEST_ASSERT_TRUE(good);
  TEST_ASSERT_EQUAL_UINT(0, s_allocatedBytes);
}

int main(int, char**) {
  UNITY_BEGIN();
  RUN_TEST(test_good_icon_draws_and_caches);
  RUN_TEST(test_missing_icon_cached_without_retry);
  RUN_TEST(test_streaming_icon_renders_under_alloc_pressure);
  RUN_TEST(test_wide_icon_draws_full_width);
  RUN_TEST(test_icon_draws_only_its_own_size);
  RUN_TEST(test_mixed_sizes_coexist_in_cache);
  RUN_TEST(test_offset_is_honoured);
  RUN_TEST(test_invalidate_reloads_wide_icon);
  RUN_TEST(test_unsafe_names_rejected);
  RUN_TEST(test_panel_sized_script_icon_draws_all_rows_and_columns);
  RUN_TEST(test_script_icon_reloads_when_panel_bounds_change);
  RUN_TEST(test_destination_size_does_not_restart_script_animation);
  RUN_TEST(test_script_icons_require_configured_panel_bounds);
  RUN_TEST(test_small_script_icon_uses_same_ram_on_larger_panels);
  RUN_TEST(test_page_icon_renders_panel_sized_animation_and_releases_on_clear);
  RUN_TEST(test_small_page_icon_uses_same_ram_on_larger_panels);
  RUN_TEST(test_four_icons_keep_independent_animation_without_reloading);
  RUN_TEST(test_duplicate_id_at_two_positions_shares_animation);
  RUN_TEST(test_four_ids_stay_pinned_and_a_fifth_does_not_thrash);
  RUN_TEST(test_unused_script_icon_cache_allocates_nothing);
  RUN_TEST(test_cache_entry_allocation_failure_is_retryable);
  RUN_TEST(test_player_allocation_failure_honours_retry_backoff);
  RUN_TEST(test_static_frame_survives_player_release_and_invalidation);
  RUN_TEST(test_asset_read_oom_retries_instead_of_caching_missing_icon);
  RUN_TEST(test_release_frees_streamed_and_static_icons_and_reloads_once);
  RUN_TEST(test_release_on_empty_set_allocates_nothing);
  RUN_TEST(test_destroying_a_set_frees_everything_it_held);
  RUN_TEST(test_missing_icon_is_looked_up_once_per_showing);
  RUN_TEST(test_release_drops_oom_backoff_and_log_is_rate_limited);
  RUN_TEST(test_two_sets_hold_the_same_icon_independently);
  RUN_TEST(test_invalidate_reaches_every_existing_set);
  RUN_TEST(test_each_set_has_its_own_four_ids);
  RUN_TEST(test_create_set_allocation_failure_returns_null_and_recovers);
  RUN_TEST(test_long_name_needs_no_allocation_once_cached);
  RUN_TEST(test_jpg_fallback_out_of_memory_is_retried_not_written_off);
  RUN_TEST(test_missing_jpg_stays_missing_until_invalidated);
  RUN_TEST(test_extreme_coordinates_draw_nothing_and_do_not_overflow);
  return UNITY_END();
}
