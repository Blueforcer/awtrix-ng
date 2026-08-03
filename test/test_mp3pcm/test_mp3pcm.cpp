
#include <unity.h>

#include <cmath>
#include <cstdio>
#include <cstdint>
#include <vector>

#include "core/audio/Mp3Decoder.h"
#include "vectors.h"

using namespace awtrix;

namespace {

struct Decoded {
  std::vector<int16_t> pcm;
  int sampleRateHz = 0;
  int channels = 0;
  int frames = 0;
};

Decoded decodeAll(const uint8_t* data, std::size_t bytes) {
  mp3::Decoder decoder;
  Decoded out;
  std::vector<int16_t> block(mp3::kMaxPcmPerFrame);

  std::size_t position = 0;
  while (position < bytes) {
    const mp3::DecodeResult result = decoder.decode(data + position, bytes - position,
                                                    block.data());
    if (result.bytesConsumed == 0) break;
    position += result.bytesConsumed;
    if (result.status != mp3::DecodeStatus::Ok) continue;
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

double bestError(const Decoded& decoded, const int16_t* reference, int channels,
                 int& bestOffset) {
  double best = 1e9;
  bestOffset = -1;
  const int limit = decoded.frames - mp3vectors::kWindowFrames;
  for (int offset = 0; offset <= limit; ++offset) {
    const double error = relativeError(decoded, reference, channels, offset);
    if (error < best) {
      best = error;
      bestOffset = offset;
    }
  }
  return best;
}

constexpr double kTolerance = 0.005;

void check(const char* name, const uint8_t* mp3, std::size_t mp3Bytes, const int16_t* reference,
           int expectedRate, int expectedChannels) {
  const Decoded decoded = decodeAll(mp3, mp3Bytes);
  TEST_ASSERT_EQUAL_INT_MESSAGE(expectedChannels, decoded.channels, name);
  TEST_ASSERT_EQUAL_INT_MESSAGE(expectedRate, decoded.sampleRateHz, name);
  TEST_ASSERT_GREATER_THAN_INT_MESSAGE(mp3vectors::kWindowFrames, decoded.frames, name);

  int offset = -1;
  const double error = bestError(decoded, reference, expectedChannels, offset);
  char message[160];
  std::snprintf(message, sizeof(message), "%s: relative RMS %.4f at offset %d", name, error,
                offset);
  TEST_ASSERT_TRUE_MESSAGE(error < kTolerance, message);
}

void test_sine_mono_64k() {
  check("sine_mono_64k", mp3vectors::ksine_mono_64k_mp3, sizeof(mp3vectors::ksine_mono_64k_mp3),
        mp3vectors::ksine_mono_64k_pcm, mp3vectors::ksine_mono_64k_rate,
        mp3vectors::ksine_mono_64k_channels);
}

void test_sine_stereo_128k() {
  check("sine_stereo_128k", mp3vectors::ksine_stereo_128k_mp3,
        sizeof(mp3vectors::ksine_stereo_128k_mp3), mp3vectors::ksine_stereo_128k_pcm,
        mp3vectors::ksine_stereo_128k_rate, mp3vectors::ksine_stereo_128k_channels);
}

void test_noise_stereo_320k() {
  check("noise_stereo_320k", mp3vectors::knoise_stereo_320k_mp3,
        sizeof(mp3vectors::knoise_stereo_320k_mp3), mp3vectors::knoise_stereo_320k_pcm,
        mp3vectors::knoise_stereo_320k_rate, mp3vectors::knoise_stereo_320k_channels);
}

void test_sweep_stereo_128k_48k() {
  check("sweep_stereo_128k_48k", mp3vectors::ksweep_stereo_128k_48k_mp3,
        sizeof(mp3vectors::ksweep_stereo_128k_48k_mp3), mp3vectors::ksweep_stereo_128k_48k_pcm,
        mp3vectors::ksweep_stereo_128k_48k_rate, mp3vectors::ksweep_stereo_128k_48k_channels);
}

void test_noise_stereo_32khz() {
  check("noise_stereo_32khz", mp3vectors::knoise_stereo_32khz_mp3,
        sizeof(mp3vectors::knoise_stereo_32khz_mp3), mp3vectors::knoise_stereo_32khz_pcm,
        mp3vectors::knoise_stereo_32khz_rate, mp3vectors::knoise_stereo_32khz_channels);
}

void test_clicks_stereo_128k() {
  check("clicks_stereo_128k", mp3vectors::kclicks_stereo_128k_mp3,
        sizeof(mp3vectors::kclicks_stereo_128k_mp3), mp3vectors::kclicks_stereo_128k_pcm,
        mp3vectors::kclicks_stereo_128k_rate, mp3vectors::kclicks_stereo_128k_channels);
}

void test_decoder_survives_truncation() {
  mp3::Decoder decoder;
  std::vector<int16_t> block(mp3::kMaxPcmPerFrame);
  const std::size_t half = sizeof(mp3vectors::ksine_mono_64k_mp3) / 2;
  std::size_t position = 0;
  int iterations = 0;
  while (position < half && iterations < 10000) {
    const mp3::DecodeResult result =
        decoder.decode(mp3vectors::ksine_mono_64k_mp3 + position, half - position, block.data());
    if (result.bytesConsumed == 0) break;
    position += result.bytesConsumed;
    ++iterations;
  }
  TEST_ASSERT_LESS_THAN_INT(10000, iterations);
}

void test_garbage_does_not_produce_audio() {
  std::vector<uint8_t> noise(4096);
  for (std::size_t i = 0; i < noise.size(); ++i) noise[i] = static_cast<uint8_t>(i * 37 + 11);
  mp3::Decoder decoder;
  std::vector<int16_t> block(mp3::kMaxPcmPerFrame);
  const mp3::DecodeResult result = decoder.decode(noise.data(), noise.size(), block.data());
  TEST_ASSERT_NOT_EQUAL(mp3::DecodeStatus::Ok, result.status);
}

void test_joins_a_high_bitrate_stream_mid_file() {
  const uint8_t* data = mp3vectors::knoise_stereo_320k_mp3;
  const std::size_t bytes = sizeof(mp3vectors::knoise_stereo_320k_mp3);

  std::size_t start = 0;
  for (int frame = 0; frame < 5; ++frame) {
    std::size_t at = 0;
    TEST_ASSERT_TRUE(mp3::findSync(data, bytes, start, at));
    mp3::FrameHeader header{};
    TEST_ASSERT_TRUE(mp3::parseHeader(data + at, bytes - at, header));
    start = at + static_cast<std::size_t>(header.frameBytes());
  }

  const Decoded out = decodeAll(data + start, bytes - start);

  TEST_ASSERT_EQUAL_INT(44100, out.sampleRateHz);
  TEST_ASSERT_EQUAL_INT(2, out.channels);
  TEST_ASSERT_GREATER_THAN_INT(4096, out.frames);
}

}

void setUp() {}
void tearDown() {}

int main(int, char**) {
  UNITY_BEGIN();
  RUN_TEST(test_sine_mono_64k);
  RUN_TEST(test_sine_stereo_128k);
  RUN_TEST(test_noise_stereo_320k);
  RUN_TEST(test_sweep_stereo_128k_48k);
  RUN_TEST(test_noise_stereo_32khz);
  RUN_TEST(test_clicks_stereo_128k);
  RUN_TEST(test_decoder_survives_truncation);
  RUN_TEST(test_garbage_does_not_produce_audio);
  RUN_TEST(test_joins_a_high_bitrate_stream_mid_file);
  return UNITY_END();
}
