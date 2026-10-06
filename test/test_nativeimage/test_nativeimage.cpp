#include <unity.h>
#include <cstdlib>
#include <cstring>
#include <new>
#include <string>
#include <vector>

namespace { bool failAllocations = false; }
void* operator new(std::size_t size) {
  if (!failAllocations) if (void* p = std::malloc(size)) return p;
  throw std::bad_alloc();
}
void* operator new(std::size_t size, const std::nothrow_t&) noexcept {
  return failAllocations ? nullptr : std::malloc(size);
}
void* operator new[](std::size_t size) { return operator new(size); }
void* operator new[](std::size_t size, const std::nothrow_t&) noexcept { return operator new(size, std::nothrow); }
void operator delete(void* p) noexcept { std::free(p); }
void operator delete(void* p, std::size_t) noexcept { std::free(p); }
void operator delete(void* p, const std::nothrow_t&) noexcept { std::free(p); }
void operator delete[](void* p) noexcept { std::free(p); }
void operator delete[](void* p, std::size_t) noexcept { std::free(p); }
void operator delete[](void* p, const std::nothrow_t&) noexcept { std::free(p); }

#include "media/AssetFile.h"
#include "media/DevicePageIcon.h"
#include "jpeg_fixture.h"
#include "../test_gifplayer/sized_gif_fixture.h"

#include "../../src/media/JpegDecoder.cpp"
#include "../../src/media/IconRenderer.cpp"
#include "../../src/media/GifPlayer.cpp"
#include "../../src/media/MicroGif.cpp"

#include <base64.hpp>

namespace {
const unsigned char* bytes = kJpeg52x16;
std::size_t byteCount = sizeof(kJpeg52x16);
bool useGif = false;
bool assetOom = false;
bool growAfterProbe = false;
std::size_t largestRead = 0;
}

namespace awtrix {
void logf(const char*, ...) {}
void logdbg(const char*, ...) {}
namespace media {
bool readAssetRange(std::string_view path, std::size_t offset, uint8_t* out,
                    std::size_t capacity, std::size_t& read, std::size_t& size) {
  read=0;size=byteCount;
  largestRead=std::max(largestRead,capacity);
  if (growAfterProbe && capacity > 1) ++size;
  if (assetOom || (path.find(".gif") != std::string::npos) != useGif || offset>size) return false;
  read=std::min(capacity,size-offset);
  std::memcpy(out,bytes+offset,read);
  return true;
}
bool readAsset(std::string_view path, PodBuffer<uint8_t>& out, bool* oom) {
  if (oom) *oom = assetOom;
  if (assetOom || (path.find(".gif") != std::string::npos) != useGif) return false;
  if (!out.resize(byteCount)) { if (oom) *oom = true; return false; }
  std::memcpy(out.data(), bytes, byteCount);
  return true;
}
}
}

void setUp() {
  bytes=kJpeg52x16; byteCount=sizeof(kJpeg52x16); useGif=false; assetOom=false;
  growAfterProbe=false;largestRead=0;
  failAllocations=false;
}
void tearDown() { failAllocations=false; }

void test_native_jpeg_retains_full_size_and_offset() {
  awtrix::DevicePageIcon icon;
  TEST_ASSERT_TRUE(icon.beginNative("photo",52,16)==awtrix::IconLoad::kGood);
  TEST_ASSERT_EQUAL_INT(52,icon.width());
  TEST_ASSERT_EQUAL_INT(16,icon.height());
  awtrix::Canvas c(60,20);
  icon.blit(c,2,3);
  const auto px=c.getPixel(53,18);
  TEST_ASSERT_UINT_WITHIN(8,32,px>>16&255);
  TEST_ASSERT_UINT_WITHIN(8,192,px>>8&255);
  TEST_ASSERT_UINT_WITHIN(8,64,px&255);
  TEST_ASSERT_EQUAL_HEX32(0,c.getPixel(54,18));
}

void test_bounded_native_uses_reservation_and_rejects_changed_file() {
  awtrix::DevicePageIcon icon;
  int width=0,height=0;
  std::size_t cost=0;
  TEST_ASSERT_TRUE(icon.nativeInfo("photo",52,16,width,height,cost));
  TEST_ASSERT_TRUE(icon.beginNativeBounded("photo",52,16,cost)==awtrix::IconLoad::kGood);
  TEST_ASSERT_EQUAL_INT(52,icon.width());
  TEST_ASSERT_TRUE(icon.beginNativeBounded("photo",52,16,cost-1)==awtrix::IconLoad::kMissing);
  TEST_ASSERT_EQUAL_INT(0,icon.width());
  growAfterProbe=true;
  TEST_ASSERT_TRUE(icon.beginNativeBounded("photo",52,16,cost)==awtrix::IconLoad::kMissing);
  TEST_ASSERT_TRUE(largestRead <= sizeof(kJpeg52x16));
  growAfterProbe=false;
  const auto gif=sizedGif(52,16);
  bytes=gif.data();byteCount=gif.size();useGif=true;
  TEST_ASSERT_TRUE(icon.nativeInfo("anim",52,16,width,height,cost));
  TEST_ASSERT_TRUE(icon.beginNativeBounded("anim",52,16,cost)==awtrix::IconLoad::kGood);
  TEST_ASSERT_EQUAL_INT(16,icon.height());
  auto falseCanvas=gif;
  falseCanvas[6]=8;falseCanvas[7]=0;falseCanvas[8]=8;falseCanvas[9]=0;
  bytes=falseCanvas.data();
  TEST_ASSERT_TRUE(icon.nativeInfo("anim",52,16,width,height,cost));
  TEST_ASSERT_TRUE(icon.beginNativeBounded("anim",52,16,cost)==awtrix::IconLoad::kMissing);
}

void test_legacy_jpeg_still_uses_eight_pixel_icon() {
  awtrix::DevicePageIcon icon;
  TEST_ASSERT_TRUE(icon.begin("photo",52,16)==awtrix::IconLoad::kGood);
  TEST_ASSERT_EQUAL_INT(8,icon.width());
  TEST_ASSERT_EQUAL_INT(8,icon.height());
  awtrix::Canvas c(52,16);
  icon.blit(c,0,0);
  TEST_ASSERT_NOT_EQUAL(0,c.getPixel(7,7));
  TEST_ASSERT_EQUAL_HEX32(0,c.getPixel(8,7));
  TEST_ASSERT_EQUAL_HEX32(0,c.getPixel(7,8));
}

void test_native_rejects_oversize_and_bad_jpeg_without_old_pixels() {
  awtrix::DevicePageIcon icon;
  TEST_ASSERT_TRUE(icon.beginNative("photo",52,16)==awtrix::IconLoad::kGood);
  TEST_ASSERT_TRUE(icon.beginNative("photo",32,8)==awtrix::IconLoad::kMissing);
  TEST_ASSERT_EQUAL_INT(0,icon.width());
  byteCount=40;
  TEST_ASSERT_TRUE(icon.beginNative("photo",52,16)==awtrix::IconLoad::kMissing);
  TEST_ASSERT_EQUAL_INT(0,icon.height());
}

void test_native_oom_is_distinguished_and_gif_stays_native() {
  awtrix::DevicePageIcon icon;
  failAllocations=true;
  int width=0,height=0;
  std::size_t cost=0;
  TEST_ASSERT_TRUE(icon.nativeInfo("photo",52,16,width,height,cost));
  TEST_ASSERT_TRUE(icon.beginNative("photo",52,16)==awtrix::IconLoad::kOom);
  failAllocations=false; useGif=true;
  const auto gif=sizedGif(52,16);
  bytes=gif.data(); byteCount=gif.size();
  TEST_ASSERT_TRUE(icon.beginNative("anim",52,16)==awtrix::IconLoad::kGood);
  TEST_ASSERT_EQUAL_INT(52,icon.width());
  TEST_ASSERT_EQUAL_INT(16,icon.height());
  awtrix::Canvas c(52,16);
  icon.advance(0); icon.blit(c,0,0);
  TEST_ASSERT_NOT_EQUAL(0,c.getPixel(51,15));
}

void test_metadata_probe_reads_dimensions_without_opening_a_decoder() {
  awtrix::DevicePageIcon icon;
  int w=0,h=0; std::size_t resident=0;
  TEST_ASSERT_TRUE(icon.nativeInfo("photo",52,16,w,h,resident));
  TEST_ASSERT_EQUAL_INT(52,w);TEST_ASSERT_EQUAL_INT(16,h);
  TEST_ASSERT_TRUE(resident>=sizeof(kJpeg52x16)+52*16*4);
  TEST_ASSERT_FALSE(icon.nativeInfo("photo",32,8,w,h,resident));
  TEST_ASSERT_FALSE(icon.nativeInfo("../photo",52,16,w,h,resident));
  TEST_ASSERT_EQUAL_INT(0,icon.width());
  const auto gif=sizedGif(52,16);useGif=true;bytes=gif.data();byteCount=gif.size();
  TEST_ASSERT_TRUE(icon.nativeInfo("anim",52,16,w,h,resident));
  TEST_ASSERT_EQUAL_INT(52,w);TEST_ASSERT_EQUAL_INT(16,h);
  TEST_ASSERT_TRUE(resident>=byteCount+52*16*13);
}

std::string dataUri(const char* type, const unsigned char* data, std::size_t length) {
  std::string b64(encode_base64_length(length), '\0');
  encode_base64(data, length, reinterpret_cast<unsigned char*>(&b64[0]));
  return std::string("data:image/") + type + ";base64," + b64;
}

void test_data_uri_images_load_natively_without_a_file() {
  useGif=true;
  const std::string jpeg=dataUri("jpeg",kJpeg52x16,sizeof(kJpeg52x16));
  awtrix::DevicePageIcon icon;
  int w=0,h=0; std::size_t resident=0;
  TEST_ASSERT_TRUE(icon.nativeInfo(jpeg,52,16,w,h,resident));
  TEST_ASSERT_EQUAL_INT(52,w);TEST_ASSERT_EQUAL_INT(16,h);
  TEST_ASSERT_TRUE(icon.beginNativeBounded(jpeg,52,16,resident)==awtrix::IconLoad::kGood);
  TEST_ASSERT_EQUAL_INT(52,icon.width());
  TEST_ASSERT_EQUAL_INT(0,static_cast<int>(largestRead));

  const auto gif=sizedGif(52,16);
  const std::string anim=dataUri("gif",gif.data(),gif.size());
  TEST_ASSERT_TRUE(icon.nativeInfo(anim,52,16,w,h,resident));
  TEST_ASSERT_TRUE(icon.beginNative(anim,52,16)==awtrix::IconLoad::kGood);
  TEST_ASSERT_EQUAL_INT(16,icon.height());
}

void test_data_uri_type_must_match_its_bytes() {
  const std::string wrong=dataUri("gif",kJpeg52x16,sizeof(kJpeg52x16));
  awtrix::DevicePageIcon icon;
  int w=0,h=0; std::size_t resident=0;
  TEST_ASSERT_FALSE(icon.nativeInfo(wrong,52,16,w,h,resident));
  TEST_ASSERT_TRUE(icon.beginNative(wrong,52,16)==awtrix::IconLoad::kMissing);
  TEST_ASSERT_TRUE(icon.begin(wrong,52,16)==awtrix::IconLoad::kMissing);
}

void test_legacy_icon_draws_a_jpeg_data_uri() {
  useGif=true;
  awtrix::DevicePageIcon icon;
  TEST_ASSERT_TRUE(icon.begin(dataUri("jpeg",kJpeg52x16,sizeof(kJpeg52x16)),52,16)==
                   awtrix::IconLoad::kGood);
  TEST_ASSERT_EQUAL_INT(8,icon.width());
  awtrix::Canvas c(52,16);
  icon.blit(c,0,0);
  TEST_ASSERT_NOT_EQUAL(0,c.getPixel(7,7));
}

void test_data_uri_decode_oom_is_reported() {
  const std::string jpeg=dataUri("jpeg",kJpeg52x16,sizeof(kJpeg52x16));
  awtrix::DevicePageIcon icon;
  failAllocations=true;
  TEST_ASSERT_TRUE(icon.beginNative(jpeg,52,16)==awtrix::IconLoad::kOom);
  failAllocations=false;
}

void test_jpeg_workspace_oom_is_reported_and_recoverable() {
  awtrix::media::JpegDecoder decoder;
  failAllocations = true;
  const auto failed = decoder.prepare(kJpeg52x16, sizeof(kJpeg52x16));
  failAllocations = false;
  TEST_ASSERT_EQUAL_INT(JDR_MEM1, failed);
  TEST_ASSERT_EQUAL_INT(JDR_OK, decoder.prepare(kJpeg52x16, sizeof(kJpeg52x16)));
  int pixels = 0;
  TEST_ASSERT_EQUAL_INT(JDR_OK, decoder.decode([](void* out, int, int, uint32_t) {
    ++*static_cast<int*>(out);
  }, &pixels));
  TEST_ASSERT_EQUAL_INT(52 * 16, pixels);
}

int main(int,char**) {
  UNITY_BEGIN();
  RUN_TEST(test_native_jpeg_retains_full_size_and_offset);
  RUN_TEST(test_bounded_native_uses_reservation_and_rejects_changed_file);
  RUN_TEST(test_legacy_jpeg_still_uses_eight_pixel_icon);
  RUN_TEST(test_native_rejects_oversize_and_bad_jpeg_without_old_pixels);
  RUN_TEST(test_native_oom_is_distinguished_and_gif_stays_native);
  RUN_TEST(test_metadata_probe_reads_dimensions_without_opening_a_decoder);
  RUN_TEST(test_data_uri_images_load_natively_without_a_file);
  RUN_TEST(test_data_uri_type_must_match_its_bytes);
  RUN_TEST(test_legacy_icon_draws_a_jpeg_data_uri);
  RUN_TEST(test_data_uri_decode_oom_is_reported);
  RUN_TEST(test_jpeg_workspace_oom_is_reported_and_recoverable);
  return UNITY_END();
}
