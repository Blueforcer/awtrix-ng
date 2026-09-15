#include <unity.h>

#include <cmath>
#include <vector>

#include "core/audio/Fft.h"

using namespace awtrix::audio;

namespace {

constexpr int kN = kFftSize;
constexpr float kPi = 3.14159265358979f;

struct Buffers {
  std::vector<float> re, im, cosTab, sinTab;
  Buffers() : re(kN, 0.f), im(kN, 0.f), cosTab(kN / 2), sinTab(kN / 2) {
    fftTwiddles(cosTab.data(), sinTab.data(), kN);
  }
  void run() { fftInPlace(re.data(), im.data(), kN, cosTab.data(), sinTab.data()); }
  float mag(int k) const { return std::sqrt(re[k] * re[k] + im[k] * im[k]); }
};

uint32_t lcg(uint32_t& s) {
  s = s * 1664525u + 1013904223u;
  return s;
}

}

void setUp() {}
void tearDown() {}

void test_impulse_is_flat() {
  Buffers b;
  b.re[0] = 1.f;
  b.run();
  for (int k = 0; k < kN; ++k) TEST_ASSERT_FLOAT_WITHIN(1e-5f, 1.f, b.mag(k));
}

void test_cosine_lands_in_its_bin() {
  Buffers b;
  for (int n = 0; n < kN; ++n) b.re[n] = std::cos(2.f * kPi * 100.f * n / kN);
  b.run();
  TEST_ASSERT_FLOAT_WITHIN(0.01f, kN / 2.f, b.mag(100));
  TEST_ASSERT_FLOAT_WITHIN(0.01f, kN / 2.f, b.mag(kN - 100));
  for (int k = 0; k < kN; ++k) {
    if (k == 100 || k == kN - 100) continue;
    TEST_ASSERT_TRUE_MESSAGE(b.mag(k) < 1e-2f, "leak outside the tone's bins");
  }
}

void test_matches_naive_dft() {
  Buffers b;
  uint32_t seed = 7;
  std::vector<float> x(kN);
  for (int n = 0; n < kN; ++n) {
    x[n] = static_cast<float>(lcg(seed) >> 8) / 16777216.f - 0.5f;
    b.re[n] = x[n];
  }
  b.run();
  const int bins[] = {0, 1, 7, 100, 511, 512};
  float maxMag = 0.f;
  for (int k = 0; k < kN; ++k) maxMag = std::max(maxMag, b.mag(k));
  for (int k : bins) {
    double sr = 0, si = 0;
    for (int n = 0; n < kN; ++n) {
      const double a = -2.0 * kPi * k * n / kN;
      sr += x[n] * std::cos(a);
      si += x[n] * std::sin(a);
    }
    TEST_ASSERT_FLOAT_WITHIN(1e-3f * maxMag, static_cast<float>(sr), b.re[k]);
    TEST_ASSERT_FLOAT_WITHIN(1e-3f * maxMag, static_cast<float>(si), b.im[k]);
  }
}

void test_parseval() {
  Buffers b;
  uint32_t seed = 99;
  double timeEnergy = 0;
  for (int n = 0; n < kN; ++n) {
    b.re[n] = static_cast<float>(lcg(seed) >> 8) / 16777216.f - 0.5f;
    timeEnergy += static_cast<double>(b.re[n]) * b.re[n];
  }
  b.run();
  double freqEnergy = 0;
  for (int k = 0; k < kN; ++k) freqEnergy += static_cast<double>(b.re[k]) * b.re[k] + static_cast<double>(b.im[k]) * b.im[k];
  TEST_ASSERT_FLOAT_WITHIN(1e-4f * static_cast<float>(timeEnergy * kN),
                           static_cast<float>(timeEnergy * kN), static_cast<float>(freqEnergy));
}

int main(int, char**) {
  UNITY_BEGIN();
  RUN_TEST(test_impulse_is_flat);
  RUN_TEST(test_cosine_lands_in_its_bin);
  RUN_TEST(test_matches_naive_dft);
  RUN_TEST(test_parseval);
  return UNITY_END();
}
