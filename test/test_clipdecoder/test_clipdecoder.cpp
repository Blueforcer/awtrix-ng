#include <unity.h>

#include <cmath>
#include <cstdio>
#include <cstdint>
#include <vector>

#include "core/audio/ClipDecoder.h"
#include "../test_mp3pcm/vectors.h"

using namespace awtrix;

namespace {

// Hands the file out in fixed slices so the walk sees the same ragged reads a
// flash file produces.
struct ChunkReader {
  const uint8_t* data;
  std::size_t size;
  std::size_t pos = 0;
  std::size_t chunk;
};

int readChunk(void* ctx, uint8_t* dst, std::size_t max) {
  ChunkReader& r = *static_cast<ChunkReader*>(ctx);
  std::size_t n = r.size - r.pos;
  if (n > r.chunk) n = r.chunk;
  if (n > max) n = max;
  for (std::size_t i = 0; i < n; ++i) dst[i] = r.data[r.pos + i];
  r.pos += n;
  return static_cast<int>(n);
}

struct Decoded {
  std::vector<int16_t> pcm;
  int sampleRateHz = 0;
  int channels = 0;
  int frames = 0;
  mp3::ClipDecoder::Step last = mp3::ClipDecoder::Step::Error;
};

Decoded decodeAll(const uint8_t* data, std::size_t bytes, std::size_t chunk) {
  mp3::Decoder decoder;
  mp3::ClipDecoder walk(decoder);
  ChunkReader reader{data, bytes, 0, chunk};
  Decoded out;
  std::vector<int16_t> block(mp3::kMaxPcmPerFrame);
  mp3::DecodeResult result;

  for (;;) {
    const mp3::ClipDecoder::Step step = walk.next(readChunk, &reader, block.data(), result);
    out.last = step;
    if (step != mp3::ClipDecoder::Step::Frame) break;
    out.sampleRateHz = result.sampleRateHz;
    out.channels = result.channels;
    out.pcm.insert(out.pcm.end(), block.begin(),
                   block.begin() + result.samples * result.channels);
  }
  out.frames = out.channels ? static_cast<int>(out.pcm.size()) / out.channels : 0;
  return out;
}

double relativeError(const Decoded& decoded, const int16_t* reference, int channels,
                     int offset) {
  if (offset < 0 || offset + mp3vectors::kWindowFrames > decoded.frames) return 1e9;
  double errorSum = 0.0;
  double referenceSum = 0.0;
  for (int i = 0; i < mp3vectors::kWindowFrames * channels; ++i) {
    const double ours = decoded.pcm[offset * channels + i];
    const double theirs = reference[i];
    errorSum += (ours - theirs) * (ours - theirs);
    referenceSum += theirs * theirs;
  }
  if (referenceSum <= 0.0) return 1e9;
  return std::sqrt(errorSum / referenceSum);
}

double bestError(const Decoded& decoded, const int16_t* reference, int channels) {
  double best = 1e9;
  const int limit = decoded.frames - mp3vectors::kWindowFrames;
  for (int offset = 0; offset <= limit; ++offset) {
    const double error = relativeError(decoded, reference, channels, offset);
    if (error < best) best = error;
  }
  return best;
}

constexpr double kTolerance = 0.005;

void checkChunked(const char* name, std::size_t chunk) {
  const Decoded decoded = decodeAll(mp3vectors::ksine_mono_64k_mp3,
                                    sizeof(mp3vectors::ksine_mono_64k_mp3), chunk);
  TEST_ASSERT_EQUAL_INT_MESSAGE(mp3vectors::ksine_mono_64k_channels, decoded.channels, name);
  TEST_ASSERT_EQUAL_INT_MESSAGE(mp3vectors::ksine_mono_64k_rate, decoded.sampleRateHz, name);
  TEST_ASSERT_TRUE_MESSAGE(decoded.last == mp3::ClipDecoder::Step::Done, name);

  const double error = bestError(decoded, mp3vectors::ksine_mono_64k_pcm,
                                 mp3vectors::ksine_mono_64k_channels);
  char message[96];
  std::snprintf(message, sizeof(message), "%s: relative RMS %.4f", name, error);
  TEST_ASSERT_TRUE_MESSAGE(error < kTolerance, message);
}

void test_whole_file_in_1k_chunks() { checkChunked("chunk_1024", 1024); }

void test_whole_file_in_7_byte_chunks() { checkChunked("chunk_7", 7); }

void test_stereo_file_matches_reference() {
  const Decoded decoded = decodeAll(mp3vectors::ksine_stereo_128k_mp3,
                                    sizeof(mp3vectors::ksine_stereo_128k_mp3), 1024);
  TEST_ASSERT_EQUAL_INT(2, decoded.channels);
  TEST_ASSERT_TRUE(decoded.last == mp3::ClipDecoder::Step::Done);
  TEST_ASSERT_TRUE(bestError(decoded, mp3vectors::ksine_stereo_128k_pcm, 2) < kTolerance);
}

void test_truncated_file_ends_with_done() {
  const std::size_t half = sizeof(mp3vectors::ksine_mono_64k_mp3) / 2;
  const Decoded decoded = decodeAll(mp3vectors::ksine_mono_64k_mp3, half, 1024);
  TEST_ASSERT_TRUE(decoded.frames > 0);
  TEST_ASSERT_TRUE(decoded.last == mp3::ClipDecoder::Step::Done);
}

void test_garbage_is_an_error() {
  std::vector<uint8_t> junk(160 * 1024);
  uint32_t state = 0x2545F491;
  for (std::size_t i = 0; i < junk.size(); ++i) {
    state = state * 1664525u + 1013904223u;
    uint8_t b = static_cast<uint8_t>(state >> 24);
    if (b == 0xFF) b = 0x00;
    junk[i] = b;
  }
  const Decoded decoded = decodeAll(junk.data(), junk.size(), 1024);
  TEST_ASSERT_EQUAL_INT(0, decoded.frames);
  TEST_ASSERT_TRUE(decoded.last == mp3::ClipDecoder::Step::Error);
}

void test_empty_file_is_an_error() {
  const uint8_t none = 0;
  const Decoded decoded = decodeAll(&none, 0, 1024);
  TEST_ASSERT_TRUE(decoded.last == mp3::ClipDecoder::Step::Error);
}

void test_junk_tail_ends_with_done() {
  std::vector<uint8_t> data(mp3vectors::ksine_mono_64k_mp3,
                            mp3vectors::ksine_mono_64k_mp3 +
                                sizeof(mp3vectors::ksine_mono_64k_mp3));
  uint32_t state = 0x9E3779B9;
  for (int i = 0; i < 200 * 1024; ++i) {
    state = state * 1664525u + 1013904223u;
    uint8_t b = static_cast<uint8_t>(state >> 24);
    if (b == 0xFF) b = 0x00;
    data.push_back(b);
  }
  const Decoded decoded = decodeAll(data.data(), data.size(), 1024);
  TEST_ASSERT_TRUE(decoded.frames > 0);
  TEST_ASSERT_TRUE(decoded.last == mp3::ClipDecoder::Step::Done);
}

}

void setUp() {}
void tearDown() {}

int main(int, char**) {
  UNITY_BEGIN();
  RUN_TEST(test_whole_file_in_1k_chunks);
  RUN_TEST(test_whole_file_in_7_byte_chunks);
  RUN_TEST(test_stereo_file_matches_reference);
  RUN_TEST(test_truncated_file_ends_with_done);
  RUN_TEST(test_garbage_is_an_error);
  RUN_TEST(test_empty_file_is_an_error);
  RUN_TEST(test_junk_tail_ends_with_done);
  UNITY_END();
  return 0;
}
