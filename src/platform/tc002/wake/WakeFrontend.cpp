// A port of TensorFlow Lite Micro's microfrontend (tensorflow/lite/experimental/microfrontend/lib,
// Copyright The TensorFlow Authors, Apache-2.0).
#include "platform/tc002/wake/WakeFrontend.h"

#include <algorithm>
#include <cstring>

#include "platform/tc002/wake/WakeFrontendTables.h"

namespace awtrix::tc002::wake {
namespace {

constexpr int kWindowBits = 12;
constexpr int kSmoothingBits = 10;
constexpr int kNoiseReductionBits = 14;
constexpr uint16_t kEvenSmoothing = static_cast<uint16_t>(0.025f * (1 << kNoiseReductionBits));
constexpr uint16_t kOddSmoothing = static_cast<uint16_t>(0.06f * (1 << kNoiseReductionBits));
constexpr uint16_t kMinSignalRemaining = static_cast<uint16_t>(0.05f * (1 << kNoiseReductionBits));
constexpr int kPcanGainBits = 21;
constexpr int kPcanSnrBits = 12;
constexpr int kPcanOutputBits = 6;
constexpr int kLogScaleShift = 6;
constexpr int kFilterbankBits = 12;
constexpr int msb32(uint32_t n) { return n == 0 ? 0 : 32 - __builtin_clz(n); }
constexpr int kCorrectionBits = msb32(static_cast<uint32_t>(WakeFft::kSize)) - 1 - kFilterbankBits / 2;
constexpr int kSnrShift = kPcanGainBits - kCorrectionBits - kPcanSnrBits;
constexpr int kChannelCount = static_cast<int>(WakeFrontend::kChannels) + 1;
constexpr int kBins = static_cast<int>(WakeFft::kHalf) + 1;

constexpr bool tablesFit() {
  if (sizeof(tables::kWindow) / sizeof(int16_t) != WakeFrontend::kWindowSamples) return false;
  if (sizeof(tables::kChannelStarts) / sizeof(int16_t) != kChannelCount) return false;
  if (sizeof(tables::kGainLut) / sizeof(int16_t) != 125) return false;
  for (int c = 0; c < kChannelCount; ++c) {
    if (tables::kChannelStarts[c] + tables::kWidths[c] > kBins) return false;
    if (tables::kWeightStarts[c] + tables::kWidths[c] > int(sizeof(tables::kWeights) / sizeof(int16_t)))
      return false;
  }
  return tables::kStartIndex > 0 && tables::kEndIndex <= kBins;
}
static_assert(tablesFit(), "WakeFrontendTables.h does not fit the frontend");

constexpr int kLogScaleLog2 = 16;
constexpr int kLogSegmentsLog2 = 7;
constexpr uint32_t kLogScale = 65536;
constexpr uint32_t kLogCoeff = 45426;
constexpr uint16_t kLogLut[] = {
  0,    224,  442,  654,  861,  1063, 1259, 1450, 1636, 1817, 1992, 2163, 2329, 2490, 2646, 2797, 2944,
  3087, 3224, 3358, 3487, 3611, 3732, 3848, 3960, 4068, 4172, 4272, 4368, 4460, 4549, 4633, 4714, 4791,
  4864, 4934, 5001, 5063, 5123, 5178, 5231, 5280, 5326, 5368, 5408, 5444, 5477, 5507, 5533, 5557, 5578,
  5595, 5610, 5622, 5631, 5637, 5640, 5641, 5638, 5633, 5626, 5615, 5602, 5586, 5568, 5547, 5524, 5498,
  5470, 5439, 5406, 5370, 5332, 5291, 5249, 5203, 5156, 5106, 5054, 5000, 4944, 4885, 4825, 4762, 4697,
  4630, 4561, 4490, 4416, 4341, 4264, 4184, 4103, 4020, 3935, 3848, 3759, 3668, 3575, 3481, 3384, 3286,
  3186, 3084, 2981, 2875, 2768, 2659, 2549, 2437, 2323, 2207, 2090, 1971, 1851, 1729, 1605, 1480, 1353,
  1224, 1094, 963,  830,  695,  559,  421,  282,  142,  0,    0};

int msb64(uint64_t n) { return n == 0 ? 0 : 64 - __builtin_clzll(n); }

int16_t wideDynamic(uint32_t x, const int16_t* lut) {
  if (x <= 2) return lut[x];
  const int16_t interval = static_cast<int16_t>(msb32(x));
  lut += 4 * interval - 6;
  const int16_t frac =
    static_cast<int16_t>(((interval < 11) ? (x << (11 - interval)) : (x >> (interval - 11))) & 0x3FF);
  int32_t result = (int32_t{lut[2]} * frac) >> 5;
  result += static_cast<int32_t>(static_cast<uint32_t>(lut[1]) << 5);
  result = static_cast<int32_t>(static_cast<uint32_t>(result) * static_cast<uint32_t>(frac));
  result = (result + (1 << 14)) >> 15;
  result += lut[0];
  return static_cast<int16_t>(result);
}

uint32_t pcanShrink(uint32_t x) {
  if (x < (2u << kPcanSnrBits)) return (x * x) >> (2 + 2 * kPcanSnrBits - kPcanOutputBits);
  return (x >> (kPcanSnrBits - kPcanOutputBits)) - (1u << kPcanOutputBits);
}

template <typename Word, Word kMost>
uint32_t squareRoot(Word num) {
  constexpr int kBits = 8 * sizeof(Word);
  if (num == 0) return 0;
  Word res = 0;
  int maxBit = kBits - msb64(num);
  maxBit |= 1;
  Word bit = Word{1} << (kBits - 1 - maxBit);
  int iterations = (kBits - 1 - maxBit) / 2 + 1;
  while (iterations--) {
    if (num >= res + bit) {
      num -= res + bit;
      res = (res >> 1u) + bit;
    } else {
      res >>= 1u;
    }
    bit >>= 2u;
  }
  if (num > res && res != kMost) ++res;
  return static_cast<uint32_t>(res);
}

uint32_t sqrt64(uint64_t num) {
  if ((num >> 32) == 0) return squareRoot<uint32_t, 0xFFFF>(static_cast<uint32_t>(num));
  return squareRoot<uint64_t, 0xFFFFFFFF>(num);
}

uint32_t log2Fraction(uint32_t x, uint32_t log2x) {
  int32_t frac = static_cast<int32_t>(x - (int64_t{1} << log2x));
  if (log2x < kLogScaleLog2) {
    frac <<= kLogScaleLog2 - log2x;
  } else {
    frac >>= log2x - kLogScaleLog2;
  }
  const uint32_t baseSeg = static_cast<uint32_t>(frac) >> (kLogScaleLog2 - kLogSegmentsLog2);
  const uint32_t segUnit = (uint32_t{1} << kLogScaleLog2) >> kLogSegmentsLog2;
  const int32_t c0 = kLogLut[baseSeg];
  const int32_t c1 = kLogLut[baseSeg + 1];
  const int32_t segBase = static_cast<int32_t>(segUnit * baseSeg);
  const int32_t relPos = ((c1 - c0) * (frac - segBase)) >> kLogScaleLog2;
  return static_cast<uint32_t>(frac + c0 + relPos);
}

uint32_t logScaled(uint32_t x) {
  const uint32_t integer = static_cast<uint32_t>(msb32(x) - 1);
  const uint32_t fraction = log2Fraction(x, integer);
  const uint32_t log2 = (integer << kLogScaleLog2) + fraction;
  const uint32_t round = kLogScale / 2;
  const uint32_t loge = static_cast<uint32_t>((uint64_t{kLogCoeff} * log2 + round) >> kLogScaleLog2);
  return ((loge << kLogScaleShift) + round) >> kLogScaleLog2;
}

}

void WakeFrontend::reset() {
  input_.fill(0);
  inputUsed_ = 0;
  estimate_.fill(0);
}

bool WakeFrontend::next(const int16_t*& samples, std::size_t& count, Frame& frame) {
  while (count > 0) {
    const std::size_t take = std::min(kWindowSamples - inputUsed_, count);
    std::memcpy(input_.data() + inputUsed_, samples, take * sizeof(int16_t));
    inputUsed_ += take;
    samples += take;
    count -= take;
    if (inputUsed_ == kWindowSamples) {
      compute(frame);
      return true;
    }
  }
  return false;
}

void WakeFrontend::compute(Frame& frame) {
  const int shift = window();
  fft_.run(fftInput_, spectrum_);
  filterbank();
  features(shift, frame);
}

int WakeFrontend::window() {
  std::array<int16_t, kWindowSamples> windowed;
  int16_t maxAbs = 0;
  for (std::size_t i = 0; i < kWindowSamples; ++i) {
    int16_t value = narrow((int32_t{input_[i]} * tables::kWindow[i]) >> kWindowBits);
    windowed[i] = value;
    if (value < 0) value = narrow(-value);
    if (value > maxAbs) maxAbs = value;
  }
  constexpr std::size_t kept = kWindowSamples - kStepSamples;
  std::memmove(input_.data(), input_.data() + kStepSamples, kept * sizeof(int16_t));
  inputUsed_ -= kStepSamples;

  const int shift = 15 - msb32(static_cast<uint32_t>(maxAbs));
  for (std::size_t i = 0; i < WakeFft::kSize; ++i) {
    fftInput_[i] = i < kWindowSamples ? narrow(static_cast<uint16_t>(windowed[i]) << shift) : 0;
  }
  return shift;
}

void WakeFrontend::filterbank() {
  for (int i = tables::kStartIndex; i < tables::kEndIndex; ++i) {
    const int32_t real = spectrum_[i].r, imag = spectrum_[i].i;
    energy_[i] = static_cast<uint32_t>(real * real) + static_cast<uint32_t>(imag * imag);
  }
  uint64_t weightSum = 0, unweightSum = 0;
  for (std::size_t chan = 0; chan <= kChannels; ++chan) {
    const uint32_t* magnitudes = energy_.data() + tables::kChannelStarts[chan];
    const int16_t* weights = tables::kWeights + tables::kWeightStarts[chan];
    const int16_t* unweights = tables::kUnweights + tables::kWeightStarts[chan];
    for (int j = 0; j < tables::kWidths[chan]; ++j) {
      const uint64_t magnitude = static_cast<uint64_t>(int64_t{static_cast<int32_t>(magnitudes[j])});
      weightSum += static_cast<uint64_t>(weights[j]) * magnitude;
      unweightSum += static_cast<uint64_t>(unweights[j]) * magnitude;
    }
    work_[chan] = weightSum;
    weightSum = unweightSum;
    unweightSum = 0;
  }
}

void WakeFrontend::features(int shift, Frame& frame) {
  for (std::size_t i = 0; i < kChannels; ++i) {
    const uint32_t signal = sqrt64(work_[i + 1]) >> shift;
    const uint32_t smoothing = (i & 1) == 0 ? kEvenSmoothing : kOddSmoothing;
    const uint32_t oneMinusSmoothing = (1u << kNoiseReductionBits) - smoothing;
    const uint32_t scaledUp = signal << kSmoothingBits;
    uint32_t estimate = static_cast<uint32_t>(
      (uint64_t{scaledUp} * smoothing + uint64_t{estimate_[i]} * oneMinusSmoothing) >> kNoiseReductionBits);
    estimate_[i] = estimate;
    if (estimate > scaledUp) estimate = scaledUp;
    const uint32_t floor =
      static_cast<uint32_t>((uint64_t{signal} * kMinSignalRemaining) >> kNoiseReductionBits);
    const uint32_t subtracted = (scaledUp - estimate) >> kSmoothingBits;
    const uint32_t reduced = subtracted > floor ? subtracted : floor;
    const uint32_t gain = static_cast<uint32_t>(int32_t{wideDynamic(estimate_[i], tables::kGainLut)});
    const uint32_t snr = static_cast<uint32_t>((uint64_t{reduced} * gain) >> kSnrShift);
    uint32_t value = pcanShrink(snr) << kCorrectionBits;
    value = value > 1 ? logScaled(value) : 0;
    frame[i] = static_cast<uint16_t>(value < 0xFFFF ? value : 0xFFFF);
  }
}

}
