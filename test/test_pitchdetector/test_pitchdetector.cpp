#include <unity.h>

#include <cmath>
#include <cstdint>
#include <cstdio>
#include <vector>

#include "core/audio/PitchDetector.h"

using namespace awtrix::audio;

namespace {

constexpr int kRate = 16000;
constexpr int kWindow = 1056;
constexpr double kPi = 3.14159265358979323846;

struct Partial {
  double ratio;
  double amp;
};

// Partials of f at the given amplitudes (full scale 1.0), plus dc and optional white noise.
std::vector<int16_t> tone(double f, const std::vector<Partial>& partials, double dc = 0, double noise = 0,
                          int rate = kRate, int samples = kWindow, double phase = 0.3, uint32_t seed = 1) {
  std::vector<int16_t> pcm(samples);
  uint32_t state = seed;
  for (int n = 0; n < samples; ++n) {
    double s = dc;
    for (const Partial& p : partials) s += p.amp * std::sin(2 * kPi * f * p.ratio * n / rate + phase * p.ratio);
    if (noise > 0) {
      state = state * 1664525u + 1013904223u;
      s += noise * ((state >> 8) / 8388608.0 - 1.0);
    }
    const double c = s > 0.999 ? 0.999 : (s < -0.999 ? -0.999 : s);
    pcm[n] = static_cast<int16_t>(std::lround(c * 32767));
  }
  return pcm;
}

std::vector<int16_t> sine(double f, double amp = 0.25, double dc = 0, int rate = kRate, double phase = 0.3) {
  return tone(f, {{1, amp}}, dc, 0, rate, rate == kRate ? kWindow : kWindow * rate / kRate, phase);
}

float detect(const std::vector<int16_t>& pcm, int rate = kRate) {
  PitchDetector detector;
  return detector.detect(pcm.data(), static_cast<int>(pcm.size()), rate);
}

double cents(double hz, double expected) { return 1200.0 * std::log2(hz / expected); }

void assertNear(double expected, float hz, double tolerance = 5.0) {
  char what[96];
  std::snprintf(what, sizeof what, "%.1f Hz answered %.3f Hz", expected, hz);
  TEST_ASSERT_TRUE_MESSAGE(hz > 0.f, what);
  TEST_ASSERT_TRUE_MESSAGE(std::fabs(cents(hz, expected)) <= tolerance, what);
}

// A sawtooth's partials at 1/k with the fundamental pulled down to `fundamental`.
std::vector<Partial> sawtooth(double fundamental, int count, double scale = 0.3) {
  std::vector<Partial> partials;
  for (int k = 1; k <= count; ++k) partials.push_back({double(k), scale * (k == 1 ? fundamental : 1.0 / k)});
  return partials;
}

}

void setUp() {}
void tearDown() {}

static void test_sines_across_the_range_land_within_five_cents() {
  for (double f : {82.0, 110.0, 220.0, 440.0, 880.0, 1500.0})
    for (double phase : {0.0, 0.3, 1.7, 4.0}) assertNear(f, detect(sine(f, 0.25, 0, kRate, phase)));
}

static void test_quiet_and_loud_sines_answer_the_same_pitch() {
  for (double amp : {0.01, 0.05, 0.9}) assertNear(440.0, detect(sine(440.0, amp)));
}

static void test_whistles_are_pure_tones_in_a_little_noise() {
  for (double f : {900.0, 1180.0, 1500.0}) assertNear(f, detect(tone(f, {{1, 0.1}}, 0, 0.004)));
}

static void test_a_weak_fundamental_does_not_jump_an_octave() {
  for (double f : {82.0, 110.0, 196.0, 330.0}) {
    const int count = static_cast<int>(3000 / f);
    assertNear(f, detect(tone(f, sawtooth(0.25, count))));
    assertNear(f, detect(tone(f, sawtooth(0.1, count))));
  }
}

static void test_a_voice_like_tone_answers_its_fundamental() {
  // Strong second and third partials over a weaker fundamental, as a sung vowel has.
  const std::vector<Partial> vowel = {{1, 0.06}, {2, 0.12}, {3, 0.1}, {4, 0.04}, {5, 0.03}, {6, 0.02}};
  for (double f : {98.0, 147.0, 262.0, 392.0}) assertNear(f, detect(tone(f, vowel, 0, 0.01)));
}

static void test_silence_and_noise_answer_zero() {
  TEST_ASSERT_EQUAL_FLOAT(0.f, detect(std::vector<int16_t>(kWindow, 0)));
  for (uint32_t seed : {1u, 7u, 1234u}) {
    TEST_ASSERT_EQUAL_FLOAT(0.f, detect(tone(0, {}, 0, 0.3, kRate, kWindow, 0, seed)));
    TEST_ASSERT_EQUAL_FLOAT(0.f, detect(tone(0, {}, 0, 0.02, kRate, kWindow, 0, seed)));
  }
}

static void test_a_tone_below_the_gate_is_silence() {
  TEST_ASSERT_EQUAL_FLOAT(0.f, detect(sine(440.0, 0.001)));
  assertNear(440.0, detect(sine(440.0, 0.006)));
}

static void test_a_dc_offset_changes_nothing() {
  assertNear(440.0, detect(sine(440.0, 0.2, 0.4)));
  assertNear(110.0, detect(sine(110.0, 0.2, -0.5)));
  TEST_ASSERT_EQUAL_FLOAT(0.f, detect(std::vector<int16_t>(kWindow, 12000)));
  TEST_ASSERT_EQUAL_FLOAT(0.f, detect(sine(440.0, 0.001, 0.6)));
}

static void test_tones_near_the_range_edges() {
  assertNear(72.0, detect(sine(72.0)));
  assertNear(1580.0, detect(sine(1580.0)));
  TEST_ASSERT_EQUAL_FLOAT(0.f, detect(sine(60.0)));
  // Refused, not answered an octave down.
  TEST_ASSERT_EQUAL_FLOAT(0.f, detect(sine(1800.0)));
  TEST_ASSERT_EQUAL_FLOAT(0.f, detect(sine(3000.0)));
}

static void test_the_same_window_always_answers_the_same_number() {
  PitchDetector detector;
  const auto voice = tone(147.0, sawtooth(0.25, 20), 0, 0.01);
  const auto other = sine(880.0);
  const float first = detector.detect(voice.data(), kWindow, kRate);
  detector.detect(other.data(), kWindow, kRate);
  const float again = detector.detect(voice.data(), kWindow, kRate);
  TEST_ASSERT_EQUAL_MEMORY(&first, &again, sizeof first);
  const float fresh = detect(voice);
  TEST_ASSERT_EQUAL_MEMORY(&first, &fresh, sizeof first);
}

static void test_consecutive_windows_of_one_tone_agree() {
  const auto stream = tone(220.0, sawtooth(0.3, 12), 0, 0, kRate, 2 * kWindow);
  PitchDetector detector;
  const float a = detector.detect(stream.data(), kWindow, kRate);
  const float b = detector.detect(stream.data() + kWindow, kWindow, kRate);
  assertNear(220.0, a);
  assertNear(220.0, b);
  TEST_ASSERT_TRUE(std::fabs(cents(a, b)) < 2.0);
}

static void test_other_rates_and_unusable_input() {
  assertNear(196.0, detect(sine(196.0, 0.25, 0, 8000), 8000));
  const auto pcm = sine(440.0);
  PitchDetector detector;
  TEST_ASSERT_EQUAL_FLOAT(0.f, detector.detect(nullptr, kWindow, kRate));
  TEST_ASSERT_EQUAL_FLOAT(0.f, detector.detect(pcm.data(), kWindow, 0));
  const auto fast = sine(440.0, 0.25, 0, 48000);
  TEST_ASSERT_EQUAL_FLOAT(0.f, detector.detect(fast.data(), static_cast<int>(fast.size()), 48000));
  TEST_ASSERT_EQUAL_FLOAT(0.f, detector.detect(pcm.data(), 400, kRate));
  TEST_ASSERT_EQUAL_FLOAT(0.f, detector.detect(pcm.data(), -1, kRate));
}

int main(int, char**) {
  UNITY_BEGIN();
  RUN_TEST(test_sines_across_the_range_land_within_five_cents);
  RUN_TEST(test_quiet_and_loud_sines_answer_the_same_pitch);
  RUN_TEST(test_whistles_are_pure_tones_in_a_little_noise);
  RUN_TEST(test_a_weak_fundamental_does_not_jump_an_octave);
  RUN_TEST(test_a_voice_like_tone_answers_its_fundamental);
  RUN_TEST(test_silence_and_noise_answer_zero);
  RUN_TEST(test_a_tone_below_the_gate_is_silence);
  RUN_TEST(test_a_dc_offset_changes_nothing);
  RUN_TEST(test_tones_near_the_range_edges);
  RUN_TEST(test_the_same_window_always_answers_the_same_number);
  RUN_TEST(test_consecutive_windows_of_one_tone_agree);
  RUN_TEST(test_other_rates_and_unusable_input);
  return UNITY_END();
}
