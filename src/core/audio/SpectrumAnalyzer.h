#pragma once

#include <cstdint>
#include <memory>

#include "core/audio/AudioStats.h"
#include "core/audio/Fft.h"

namespace awtrix {
namespace audio {

// Turns one decoded frame into band levels, a loudness and a beat flag. Deterministic: the same
// frame sequence always answers the same numbers, which is what the host tests lean on.
class SpectrumAnalyzer {
 public:
  static constexpr float kMinBandHz = 50.f;
  static constexpr float kMaxBandHz = 16000.f;
  // Dynamic range on the panel, below a reference that follows the loudest band. The loudness
  // gets a much narrower one: compressed music moves only a few dB, and that must show.
  static constexpr float kRangeDb = 45.f;
  // The loudness window spans the quietest to the loudest recent frame, never narrower than
  // kLevelMinRangeDb (a steady tone sits at the top) and never wider than kLevelRangeDb.
  static constexpr float kLevelRangeDb = 20.f;
  static constexpr float kLevelMinRangeDb = 6.f;
  static constexpr float kAgcDecayDbPerSec = 8.f;
  // Band dB of a -60 dBFS tone; the reference never sinks below it, so hiss is not amplified.
  static constexpr float kAgcFloorDb = -12.f;
  static constexpr float kLevelFloorDb = -60.f;
  static constexpr float kSilenceDb = -80.f;
  static constexpr float kBassLowHz = 50.f;
  static constexpr float kBassHighHz = 150.f;
  static constexpr float kBeatRatio = 1.6f;
  static constexpr int kBeatAvgMs = 1000;
  static constexpr int kBeatRefractoryMs = 250;
  static constexpr int kBeatWarmupFrames = 8;

  // pcm is interleaved int16, channels 1 or 2. False for unusable arguments or when the scratch
  // block could not be allocated.
  bool analyze(const int16_t* pcm, int samples, int channels, int sampleRateHz, FrameStats& out);
  void reset();

  int bandForHz(float hz) const;
  int bandLowBin(int band) const { return edges_[band]; }
  int bandHighBin(int band) const { return edges_[band + 1]; }
  int sampleRateHz() const { return rateHz_; }

 private:
  bool ensureScratch();
  void rebuildBands(int sampleRateHz);

  // re, im, window, cos, sin: 4 * kFftSize floats, on the heap so the audio task's stack stays small.
  std::unique_ptr<float[]> scratch_;
  int edges_[kBandCount + 1] = {};
  int bassLo_ = 1;
  int bassHi_ = 2;
  int rateHz_ = 0;
  float ref_ = kAgcFloorDb;
  float levelRef_ = kLevelFloorDb;
  // Starts above any reading so the first frame sets it; from then on it only creeps up.
  float levelMin_ = 1e9f;
  float bassAvg_ = 0.f;
  int samplesSinceBeat_ = 1 << 30;
  int frames_ = 0;
};

}
}
