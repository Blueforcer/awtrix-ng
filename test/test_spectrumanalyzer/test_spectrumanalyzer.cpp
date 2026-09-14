#include <unity.h>

#include <cmath>
#include <cstdint>
#include <vector>

#include "core/audio/SpectrumAnalyzer.h"

using namespace awtrix::audio;

namespace {

constexpr int kFrames = 1152;
constexpr float kPi = 3.14159265358979f;

struct Gen {
  int rate = 44100;
  int channels = 2;
  double phase = 0;
  std::vector<int16_t> pcm;
  Gen() : pcm(kFrames * 2) {}

  // One frame of a sine at freq/amp; extra() adds a second signal per sample index.
  template <typename F>
  void fill(float freq, float amp, F extra, float rightSign = 1.f) {
    for (int n = 0; n < kFrames; ++n) {
      const float s = amp * static_cast<float>(std::sin(phase)) + extra(n);
      phase += 2.0 * kPi * freq / rate;
      const float c = s > 0.95f ? 0.95f : (s < -0.95f ? -0.95f : s);
      pcm[n * channels] = static_cast<int16_t>(c * 32767.f);
      if (channels == 2) pcm[n * channels + 1] = static_cast<int16_t>(rightSign * c * 32767.f);
    }
  }
  void fill(float freq, float amp) {
    fill(freq, amp, [](int) { return 0.f; });
  }
  bool run(SpectrumAnalyzer& a, FrameStats& out) {
    return a.analyze(pcm.data(), kFrames, channels, rate, out);
  }
};

int argmax(const FrameStats& s) {
  int best = 0;
  for (int i = 1; i < kBandCount; ++i)
    if (s.bands[i] > s.bands[best]) best = i;
  return best;
}

}

void setUp() {}
void tearDown() {}

void test_silence_is_all_zero() {
  SpectrumAnalyzer a;
  Gen g;
  FrameStats s;
  for (int f = 0; f < 10; ++f) {
    g.fill(1000.f, 0.f);
    TEST_ASSERT_TRUE(g.run(a, s));
  }
  for (int i = 0; i < kBandCount; ++i) TEST_ASSERT_EQUAL_UINT8(0, s.bands[i]);
  TEST_ASSERT_EQUAL_UINT8(0, s.level);
  TEST_ASSERT_FALSE(s.beat);
}

void test_1khz_lands_in_its_band() {
  SpectrumAnalyzer a;
  Gen g;
  FrameStats s;
  for (int f = 0; f < 20; ++f) {
    g.fill(1000.f, 0.9f);
    TEST_ASSERT_TRUE(g.run(a, s));
  }
  const int band = a.bandForHz(1000.f);
  TEST_ASSERT_EQUAL_INT(band, argmax(s));
  TEST_ASSERT_EQUAL_UINT8(255, s.bands[band]);
  for (int i = 0; i < kBandCount; ++i)
    if (i <= band - 3 || i >= band + 3) TEST_ASSERT_TRUE(s.bands[i] < 40);
}

void test_band_edges_follow_the_sample_rate() {
  SpectrumAnalyzer a;
  Gen g;
  FrameStats s;
  g.rate = 48000;
  g.fill(1000.f, 0.5f);
  TEST_ASSERT_TRUE(g.run(a, s));
  TEST_ASSERT_EQUAL_INT(48000, a.sampleRateHz());
  const float binHz48 = 48000.f / kFftSize;
  TEST_ASSERT_FLOAT_WITHIN(2 * binHz48, 16000.f, a.bandHighBin(kBandCount - 1) * binHz48);
  TEST_ASSERT_TRUE(a.bandLowBin(a.bandForHz(1000.f)) * binHz48 <= 1000.f);
  TEST_ASSERT_TRUE(a.bandHighBin(a.bandForHz(1000.f)) * binHz48 > 1000.f);
  for (int i = 0; i < kBandCount; ++i) {
    TEST_ASSERT_TRUE(a.bandHighBin(i) > a.bandLowBin(i));
    if (i > 0) TEST_ASSERT_EQUAL_INT(a.bandHighBin(i - 1), a.bandLowBin(i));
  }

  g.rate = 22050;
  g.fill(1000.f, 0.5f);
  TEST_ASSERT_TRUE(g.run(a, s));
  const float binHz22 = 22050.f / kFftSize;
  TEST_ASSERT_FLOAT_WITHIN(2 * binHz22, 11025.f, a.bandHighBin(kBandCount - 1) * binHz22);
  for (int i = 0; i < kBandCount; ++i) TEST_ASSERT_TRUE(a.bandHighBin(i) > a.bandLowBin(i));
}

void test_stereo_is_mixed_to_mono() {
  SpectrumAnalyzer a;
  Gen g;
  FrameStats s;
  for (int f = 0; f < 5; ++f) {
    g.fill(1000.f, 0.9f, [](int) { return 0.f; }, -1.f);
    TEST_ASSERT_TRUE(g.run(a, s));
  }
  for (int i = 0; i < kBandCount; ++i) TEST_ASSERT_EQUAL_UINT8(0, s.bands[i]);
  TEST_ASSERT_EQUAL_UINT8(0, s.level);
}

void test_quiet_track_fills_the_panel() {
  SpectrumAnalyzer a;
  Gen g;
  FrameStats s;
  g.fill(1000.f, 0.0316f);
  TEST_ASSERT_TRUE(g.run(a, s));
  TEST_ASSERT_EQUAL_UINT8(255, s.bands[argmax(s)]);
}

void test_agc_recovers_after_a_loud_passage() {
  SpectrumAnalyzer a;
  Gen g;
  FrameStats s;
  for (int f = 0; f < 20; ++f) {
    g.fill(1000.f, 0.9f);
    g.run(a, s);
  }
  g.fill(1000.f, 0.0316f);
  g.run(a, s);
  const int peak = s.bands[argmax(s)];
  TEST_ASSERT_TRUE(peak >= 60 && peak <= 110);
  for (int f = 0; f < 200; ++f) {
    g.fill(1000.f, 0.0316f);
    g.run(a, s);
  }
  TEST_ASSERT_TRUE(s.bands[argmax(s)] >= 250);
}

void test_level_tracks_rms() {
  SpectrumAnalyzer a;
  Gen g;
  FrameStats s;
  g.fill(1000.f, 1.0f);
  g.run(a, s);
  TEST_ASSERT_EQUAL_UINT8(255, s.level);
  g.fill(1000.f, 0.1f);
  g.run(a, s);
  TEST_ASSERT_UINT8_WITHIN(12, 142, s.level);
  g.fill(1000.f, 0.f);
  g.run(a, s);
  TEST_ASSERT_EQUAL_UINT8(0, s.level);
}

void test_each_pulse_is_one_beat() {
  SpectrumAnalyzer a;
  Gen g;
  FrameStats s;
  const int frames = 5 * 44100 / kFrames;
  int beats = 0;
  int lastBeatFrame = -100;
  long sample = 0;
  for (int f = 0; f < frames; ++f) {
    const long base = sample;
    g.fill(1000.f, 0.0316f, [&](int n) {
      const double t = std::fmod((base + n) / 44100.0, 0.5);
      if (t >= 0.1) return 0.f;
      return static_cast<float>(0.8 * std::exp(-t * 18.0) * std::sin(2.0 * kPi * 60.0 * t));
    });
    sample += kFrames;
    g.run(a, s);
    if (s.beat) {
      TEST_ASSERT_TRUE(f - lastBeatFrame >= 10);
      lastBeatFrame = f;
      ++beats;
    }
  }
  TEST_ASSERT_TRUE(beats >= 9 && beats <= 10);
}

void test_a_steady_tone_is_not_a_beat() {
  SpectrumAnalyzer a;
  Gen g;
  FrameStats s;
  int beats = 0;
  for (int f = 0; f < 3 * 44100 / kFrames; ++f) {
    g.fill(60.f, 0.8f);
    g.run(a, s);
    if (s.beat) ++beats;
  }
  TEST_ASSERT_EQUAL_INT(0, beats);
}

void test_short_frame_is_zero_padded() {
  SpectrumAnalyzer a;
  Gen g;
  FrameStats s;
  g.fill(1000.f, 0.9f);
  TEST_ASSERT_TRUE(a.analyze(g.pcm.data(), 576, 2, 44100, s));
  TEST_ASSERT_EQUAL_INT(a.bandForHz(1000.f), argmax(s));
}

void test_bad_args_answer_false() {
  SpectrumAnalyzer a;
  Gen g;
  FrameStats s;
  g.fill(1000.f, 0.9f);
  TEST_ASSERT_FALSE(a.analyze(g.pcm.data(), kFrames, 2, 0, s));
  TEST_ASSERT_FALSE(a.analyze(g.pcm.data(), kFrames, 3, 44100, s));
  TEST_ASSERT_FALSE(a.analyze(g.pcm.data(), 0, 2, 44100, s));
}

int main(int, char**) {
  UNITY_BEGIN();
  RUN_TEST(test_silence_is_all_zero);
  RUN_TEST(test_1khz_lands_in_its_band);
  RUN_TEST(test_band_edges_follow_the_sample_rate);
  RUN_TEST(test_stereo_is_mixed_to_mono);
  RUN_TEST(test_quiet_track_fills_the_panel);
  RUN_TEST(test_agc_recovers_after_a_loud_passage);
  RUN_TEST(test_level_tracks_rms);
  RUN_TEST(test_each_pulse_is_one_beat);
  RUN_TEST(test_a_steady_tone_is_not_a_beat);
  RUN_TEST(test_short_frame_is_zero_padded);
  RUN_TEST(test_bad_args_answer_false);
  return UNITY_END();
}
