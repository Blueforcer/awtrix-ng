#include "core/audio/SpectrumAnalyzer.h"

#include <algorithm>
#include <cmath>
#include <limits>
#include <new>

namespace awtrix {
namespace audio {

namespace {

constexpr int kN = kFftSize;
constexpr float kPi = 3.14159265358979f;

float toDb(float power) { return 10.f * std::log10(power + 1e-12f); }

uint8_t scale(float db, float ref, float range) {
  const float t = (db - (ref - range)) / range;
  const float c = t < 0.f ? 0.f : (t > 1.f ? 1.f : t);
  return static_cast<uint8_t>(c * 255.f + 0.5f);
}

}

bool SpectrumAnalyzer::ensureScratch() {
  if (scratch_) return true;
  scratch_.reset(new (std::nothrow) float[4 * kN]);
  if (!scratch_) return false;
  float* window = scratch_.get() + 2 * kN;
  for (int n = 0; n < kN; ++n) window[n] = 0.5f - 0.5f * std::cos(2.f * kPi * n / kN);
  fftTwiddles(window + kN, window + kN + kN / 2, kN);
  return true;
}

void SpectrumAnalyzer::rebuildBands(int sampleRateHz) {
  rateHz_ = sampleRateHz;
  const float binHz = static_cast<float>(sampleRateHz) / kN;
  const float top = std::min(kMaxBandHz, sampleRateHz / 2.f);
  const float ratio = std::pow(top / kMinBandHz, 1.f / kBandCount);
  edges_[0] = std::max(1, static_cast<int>(std::lround(kMinBandHz / binHz)));
  for (int i = 1; i <= kBandCount; ++i) {
    const int wanted = static_cast<int>(std::lround(kMinBandHz * std::pow(ratio, i) / binHz));
    edges_[i] = std::min(kN / 2, std::max(wanted, edges_[i - 1] + 1));
  }
  bassLo_ = std::max(1, static_cast<int>(std::lround(kBassLowHz / binHz)));
  bassHi_ = std::max(bassLo_ + 1, static_cast<int>(std::lround(kBassHighHz / binHz)));
}

int SpectrumAnalyzer::bandForHz(float hz) const {
  const int bin = static_cast<int>(hz * kN / (rateHz_ > 0 ? rateHz_ : 44100));
  for (int i = 0; i < kBandCount; ++i)
    if (bin < edges_[i + 1]) return i;
  return kBandCount - 1;
}

void SpectrumAnalyzer::reset() {
  ref_ = kAgcFloorDb;
  levelRef_ = kLevelFloorDb;
  levelMin_ = 1e9f;
  bassAvg_ = 0.f;
  samplesSinceBeat_ = 1 << 30;
  frames_ = 0;
}

bool SpectrumAnalyzer::analyze(const int16_t* pcm, int samples, int channels, int sampleRateHz,
                               FrameStats& out) {
  if (!pcm || samples <= 0 || (channels != 1 && channels != 2) || sampleRateHz < 8000) return false;
  if (!ensureScratch()) return false;
  if (sampleRateHz != rateHz_) rebuildBands(sampleRateHz);

  float* re = scratch_.get();
  float* im = re + kN;
  const float* window = im + kN;
  const float* cosTab = window + kN;
  const float* sinTab = cosTab + kN / 2;

  const int used = std::min(samples, kN);
  float mean = 0.f;
  if (scaling_ == Scaling::Fixed) {
    for (int n = 0; n < used; ++n) {
      mean += channels == 2 ? (pcm[n * 2] + pcm[n * 2 + 1]) * 0.5f : pcm[n];
    }
    mean /= used;
  }
  double sumSq = 0;
  float windowPower = 0.f;
  for (int n = 0; n < used; ++n) {
    float x = pcm[n * channels];
    if (channels == 2) x = (x + pcm[n * 2 + 1]) * 0.5f;
    x = (x - mean) * (1.f / 32768.f);
    sumSq += static_cast<double>(x) * x;
    re[n] = x * window[n];
    im[n] = 0.f;
    windowPower += window[n] * window[n];
  }
  for (int n = used; n < kN; ++n) re[n] = im[n] = 0.f;

  const float levelDb = 20.f * std::log10(static_cast<float>(std::sqrt(sumSq / used)) + 1e-9f);
  const float secs = static_cast<float>(samples) / sampleRateHz;
  // The warm-up and refractory counters saturate.
  if (frames_ <= kBeatWarmupFrames) ++frames_;
  const int elapsedSinceBeat = static_cast<int>(std::min<int64_t>(
      std::numeric_limits<int>::max(), static_cast<int64_t>(samplesSinceBeat_) + samples));
  if (levelDb < kSilenceDb) {
    out = FrameStats{};
    samplesSinceBeat_ = elapsedSinceBeat;
    ref_ = std::max(kAgcFloorDb, ref_ - kAgcDecayDbPerSec * secs);
    levelRef_ = std::max(kLevelFloorDb, levelRef_ - kAgcDecayDbPerSec * secs);
    return true;
  }

  fftInPlace(re, im, kN, cosTab, sinTab);
  for (int k = 1; k <= kN / 2; ++k) re[k] = re[k] * re[k] + im[k] * im[k];

  float bandDb[kBandCount];
  float frameMax = -200.f;
  for (int i = 0; i < kBandCount; ++i) {
    float energy = 0.f;
    for (int k = edges_[i]; k < edges_[i + 1]; ++k) energy += re[k];
    bandDb[i] = toDb(energy);
    frameMax = std::max(frameMax, bandDb[i]);
  }
  const bool fixed = scaling_ == Scaling::Fixed;
  // Parseval normalization for the one-sided, windowed spectrum. No frame-dependent gain.
  ref_ = fixed ? kFixedUpperDb + toDb(kN * windowPower * 0.5f)
               : std::max(frameMax, std::max(kAgcFloorDb, ref_ - kAgcDecayDbPerSec * secs));
  const float range = fixed ? kFixedRangeDb : kRangeDb;
  for (int i = 0; i < kBandCount; ++i) out.bands[i] = scale(bandDb[i], ref_, range);

  if (fixed) {
    out.level = scale(levelDb, kFixedUpperDb, kFixedRangeDb);
  } else {
    levelRef_ = std::max(levelDb, std::max(kLevelFloorDb, levelRef_ - kAgcDecayDbPerSec * secs));
    levelMin_ = std::min(levelDb, levelMin_ + kAgcDecayDbPerSec * secs);
    const float span = std::min(kLevelRangeDb, std::max(kLevelMinRangeDb, levelRef_ - levelMin_));
    out.level = scale(levelDb, levelRef_, span);
  }

  float bass = 0.f;
  for (int k = bassLo_; k < bassHi_; ++k) bass += re[k];
  const bool warm = frames_ > kBeatWarmupFrames;
  const bool rested = samplesSinceBeat_ >= sampleRateHz * kBeatRefractoryMs / 1000;
  out.beat = warm && rested && bass > kBeatRatio * bassAvg_ && toDb(bass) > ref_ - range;
  samplesSinceBeat_ = out.beat ? 0 : elapsedSinceBeat;
  // The average adopts the reading outright while warming up, so a steady tone never starts as a beat.
  const float alpha = warm ? std::min(1.f, secs * 1000.f / kBeatAvgMs) : 1.f;
  bassAvg_ += (bass - bassAvg_) * alpha;
  return true;
}

}
}
