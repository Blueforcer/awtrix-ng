#include "core/audio/Mp3Decoder.h"

#include <cmath>
#include <cstring>

#include "core/audio/Mp3Bitstream.h"
#include "core/audio/Mp3Huffman.h"
#include "core/audio/Mp3Tables.h"
#include "core/audio/Mp3Window.h"

namespace awtrix {
namespace mp3 {

namespace {

constexpr int kSubbands = 32;
constexpr float kPi = 3.14159265358979323846f;
constexpr int kSamplesPerSubband = 18;

// Cosine and window constants for the IMDCT and the synthesis filterbank. Computed once on first
// decode instead of being stored as literals, which keeps them out of flash.
struct CosineTables {
  float imdct36[18][18];
  float imdct12[6][6];
  float dctSec32[16];
  float dctSec16[8];
  float dctSec8[4];
  float dctSec4[2];
  float dctSec2[1];
  float window512[512];

  CosineTables() {
    for (int i = 0; i < 18; ++i)
      for (int k = 0; k < 18; ++k)
        imdct36[i][k] = std::cos(kPi / 72.0f * (2 * (i + 9) + 1 + 18) * (2 * k + 1));
    for (int i = 0; i < 6; ++i)
      for (int k = 0; k < 6; ++k)
        imdct12[i][k] = std::cos(kPi / 24.0f * (2 * (i + 3) + 1 + 6) * (2 * k + 1));
    for (int k = 0; k < 16; ++k) dctSec32[k] = 0.5f / std::cos(kPi * (2 * k + 1) / 64.0f);
    for (int k = 0; k < 8; ++k) dctSec16[k] = 0.5f / std::cos(kPi * (2 * k + 1) / 32.0f);
    for (int k = 0; k < 4; ++k) dctSec8[k] = 0.5f / std::cos(kPi * (2 * k + 1) / 16.0f);
    for (int k = 0; k < 2; ++k) dctSec4[k] = 0.5f / std::cos(kPi * (2 * k + 1) / 8.0f);
    dctSec2[0] = 0.5f / std::cos(kPi / 4.0f);
    for (int i = 0; i < 512; ++i) window512[i] = synthesisWindow(i);
  }
};

const CosineTables& cosines() {
  static const CosineTables tables;
  return tables;
}

// Lee's recursive DCT factorisation: an N-point transform becomes two N/2-point ones. halfSec
// walks down the secant tables in step with the recursion.
template <int N>
inline void dctLee(const float* x, float* out, const float* const* halfSec) {
  const float* sec = halfSec[0];
  float even[N / 2];
  float odd[N / 2];
  for (int k = 0; k < N / 2; ++k) {
    even[k] = x[k] + x[N - 1 - k];
    odd[k] = (x[k] - x[N - 1 - k]) * sec[k];
  }
  float evenOut[N / 2];
  float oddOut[N / 2];
  dctLee<N / 2>(even, evenOut, halfSec + 1);
  dctLee<N / 2>(odd, oddOut, halfSec + 1);
  for (int m = 0; m < N / 2 - 1; ++m) {
    out[2 * m] = evenOut[m];
    out[2 * m + 1] = oddOut[m] + oddOut[m + 1];
  }
  out[N - 2] = evenOut[N / 2 - 1];
  out[N - 1] = oddOut[N / 2 - 1];
}

template <>
inline void dctLee<1>(const float* x, float* out, const float* const*) {
  out[0] = x[0];
}

// Truncated IMDCT matrix product. Columns is how many leading coefficients can be non-zero; the
// remaining ones are known zero, so their columns are skipped entirely.
template <int Columns>
inline void imdctRows(const float* input, const float (*rows)[18], float* raw) {
  for (int i = 0; i < 18; i += 2) {
    const float* row0 = rows[i];
    const float* row1 = rows[i + 1];
    float a0 = 0.0f, a1 = 0.0f, b0 = 0.0f, b1 = 0.0f;
    for (int k = 0; k < Columns; k += 2) {
      const float x0 = input[k];
      const float x1 = input[k + 1];
      a0 += x0 * row0[k];
      b0 += x0 * row1[k];
      a1 += x1 * row0[k + 1];
      b1 += x1 * row1[k + 1];
    }
    raw[9 + i] = a0 + a1;
    raw[10 + i] = b0 + b1;
  }
}

// magnitude^(4/3) for requantisation. Small magnitudes dominate, and a table lookup there keeps
// pow() out of the inner loop.
float powerOfFourThirds(int magnitude) {
  if (magnitude < kPow43Size) return kPow43[magnitude];
  return std::pow(static_cast<float>(magnitude), 4.0f / 3.0f);
}

const uint8_t* bandWidths(const FrameHeader& header, const GranuleInfo& granule) {
  const int rate = rateIndex(header.sampleRateHz);
  if (granule.blockType != 2) return kBandsLong[rate];
  return granule.mixedBlock ? kBandsMixed[rate] : kBandsShort[rate];
}

// Splits the big-values area into the three Huffman-table regions. Short blocks use a fixed split
// at line 36; long blocks derive theirs from region0_count/region1_count in the side info.
void regionBoundaries(const FrameHeader& header, const GranuleInfo& granule, int& region1,
                      int& region2) {
  if (granule.windowSwitching && granule.blockType == 2) {
    region1 = 36;
    region2 = kSamplesPerGranule;
    return;
  }
  const uint8_t* widths = kBandsLong[rateIndex(header.sampleRateHz)];
  int start = 0;
  int line = 0;
  for (int band = 0; widths[band] != 0; ++band) {
    if (band == granule.region0Count + 1) start = line;
    if (band == granule.region0Count + granule.region1Count + 2) {
      region1 = start;
      region2 = line;
      return;
    }
    line += widths[band];
  }
  region1 = start;
  region2 = kSamplesPerGranule;
}

}

void Decoder::reset() {
  std::memset(spectrum_, 0, sizeof(spectrum_));
  std::memset(overlap_, 0, sizeof(overlap_));
  std::memset(synthesis_, 0, sizeof(synthesis_));
  std::memset(scalefactors_, 0, sizeof(scalefactors_));
  std::memset(intensityPosition_, 0, sizeof(intensityPosition_));
  for (int ch = 0; ch < kMaxChannels; ++ch) synthesisOffset_[ch] = 0;
  reservoirFill_ = 0;
}


namespace {

int readScalefactors(BitReader& bits, const SideInfo& side, int granule, int channel,
                     int* out) {
  const GranuleInfo& g = side.granules[granule][channel];
  const int slen1 = kSlen1[g.scalefacCompress];
  const int slen2 = kSlen2[g.scalefacCompress];
  const std::size_t start = bits.bitPos();

  if (g.blockType == 2) {
    if (g.mixedBlock) {
      for (int band = 0; band < 8; ++band) out[band] = static_cast<int>(bits.read(slen1));
      for (int band = 3; band < 12; ++band) {
        const int width = band < 6 ? slen1 : slen2;
        for (int window = 0; window < 3; ++window)
          out[8 + (band - 3) * 3 + window] = static_cast<int>(bits.read(width));
      }
    } else {
      for (int band = 0; band < 12; ++band) {
        const int width = band < 6 ? slen1 : slen2;
        for (int window = 0; window < 3; ++window)
          out[band * 3 + window] = static_cast<int>(bits.read(width));
      }
    }
    return static_cast<int>(bits.bitPos() - start);
  }

  // Long blocks: scfsi lets granule 1 reuse granule 0's scalefactors for a whole group, in which
  // case those bands are simply absent from the bitstream.
  static const int kGroupStart[5] = {0, 6, 11, 16, 21};
  for (int group = 0; group < 4; ++group) {
    const int width = group < 2 ? slen1 : slen2;
    for (int band = kGroupStart[group]; band < kGroupStart[group + 1]; ++band) {
      if (granule == 1 && side.scfsi[channel][group]) continue;
      out[band] = static_cast<int>(bits.read(width));
    }
  }
  return static_cast<int>(bits.bitPos() - start);
}

bool readSpectrum(BitReader& bits, const FrameHeader& header, const GranuleInfo& granule,
                  std::size_t huffmanEnd, int* coefficients) {
  std::memset(coefficients, 0, sizeof(int) * kSamplesPerGranule);

  int region1 = 0;
  int region2 = 0;
  regionBoundaries(header, granule, region1, region2);

  const int bigValueLines = granule.bigValues * 2;
  int line = 0;
  for (; line < bigValueLines && line < kSamplesPerGranule; line += 2) {
    const int region = line < region1 ? 0 : (line < region2 ? 1 : 2);
    const int tableNumber = granule.tableSelect[region];
    const int linbits = kLinbits[tableNumber];

    Pair pair{};
    if (!decodePair(bits, kBigValueTables[tableNumber], pair)) return false;

    // 15 is the escape code: the true magnitude is 15 plus linbits extra bits. A sign bit follows
    // each non-zero value.
    int values[2] = {pair.x, pair.y};
    for (int i = 0; i < 2; ++i) {
      if (values[i] == 15 && linbits > 0) values[i] += static_cast<int>(bits.read(linbits));
      if (values[i] != 0 && bits.read(1)) values[i] = -values[i];
    }
    coefficients[line] = values[0];
    coefficients[line + 1] = values[1];
    if (bits.overrun()) return false;
  }

  // count1 region: quadruples of -1/0/+1 filling the lines above big_values until the granule's
  // bit budget runs out. Running dry is normal, not an error.
  const HuffTable& table = kCount1Tables[granule.count1TableSelect ? 1 : 0];
  while (bits.bitPos() < huffmanEnd && line + 4 <= kSamplesPerGranule) {
    Quad quad{};
    if (!decodeQuad(bits, table, quad)) break;
    int values[4] = {quad.v, quad.w, quad.x, quad.y};
    for (int i = 0; i < 4; ++i) {
      if (values[i] != 0 && bits.read(1)) values[i] = -values[i];
      coefficients[line + i] = values[i];
    }
    line += 4;
    if (bits.overrun()) break;
  }
  return true;
}

}

void Decoder::requantize(const FrameHeader& header, const GranuleInfo& granule,
                         const int* coefficients, int channel) {
  const uint8_t* widths = bandWidths(header, granule);
  const float multiplier = granule.scalefacScale ? 1.0f : 0.5f;
  const bool shortBlock = granule.blockType == 2;
  const int longBandsInMixed = granule.mixedBlock ? 8 : 0;

  int line = 0;
  for (int band = 0; widths[band] != 0 && line < kSamplesPerGranule; ++band) {
    const int width = widths[band];
    const bool isShortBand = shortBlock && band >= longBandsInMixed;

    // Requantisation exponent: global_gain biased by 210 and scaled by 1/4, less the scalefactor
    // (halved unless scalefac_scale is set) and, for short blocks, the subblock gain.
    float exponent = (granule.globalGain - 210) * 0.25f;
    if (isShortBand) {
      const int window = (band - longBandsInMixed) % 3;
      exponent -= 2.0f * granule.subblockGain[window];
      exponent -= multiplier * scalefactors_[channel][band];
    } else {
      int scalefactor = scalefactors_[channel][band];
      if (granule.preflag && band < 21) scalefactor += kPreemphasis[band];
      exponent -= multiplier * scalefactor;
    }
    const float scale = std::exp2(exponent);

    for (int i = 0; i < width && line < kSamplesPerGranule; ++i, ++line) {
      const int value = coefficients[line];
      if (value == 0) {
        spectrum_[channel][line] = 0.0f;
        continue;
      }
      const float magnitude = powerOfFourThirds(value < 0 ? -value : value) * scale;
      spectrum_[channel][line] = value < 0 ? -magnitude : magnitude;
    }
  }
  for (; line < kSamplesPerGranule; ++line) spectrum_[channel][line] = 0.0f;
}


void Decoder::applyStereo(const FrameHeader& header, const SideInfo& side, int granule) {
  if (header.channels() != 2) return;
  const bool midSide = header.channelMode == ChannelMode::JointStereo &&
                       (header.modeExtension & 0x02) != 0;
  const bool intensity = header.channelMode == ChannelMode::JointStereo &&
                         (header.modeExtension & 0x01) != 0;
  if (!midSide && !intensity) return;

  const GranuleInfo& g = side.granules[granule][0];
  const uint8_t* widths = bandWidths(header, g);

  // Intensity stereo begins above the highest band that still carries right-channel data; from
  // there up, the left channel holds the signal and the right scalefactor holds the pan position.
  int intensityStart = kSamplesPerGranule;
  if (intensity) {
    intensityStart = 0;
    int line = 0;
    for (int band = 0; widths[band] != 0; ++band) {
      bool nonZero = false;
      for (int i = 0; i < widths[band] && line + i < kSamplesPerGranule; ++i)
        if (spectrum_[1][line + i] != 0.0f) nonZero = true;
      if (nonZero) intensityStart = line + widths[band];
      line += widths[band];
    }
  }

  int line = 0;
  for (int band = 0; widths[band] != 0 && line < kSamplesPerGranule; ++band) {
    const int width = widths[band];
    const bool useIntensity = intensity && line >= intensityStart;

    if (useIntensity) {
      const int position = scalefactors_[1][band];
      if (position < 7) {
        const float left = kIntensityPan[position][0];
        const float right = kIntensityPan[position][1];
        for (int i = 0; i < width && line + i < kSamplesPerGranule; ++i) {
          const float value = spectrum_[0][line + i];
          spectrum_[0][line + i] = value * left;
          spectrum_[1][line + i] = value * right;
        }
      }
    } else if (midSide) {
      constexpr float kInverseRootTwo = 0.70710678f;
      for (int i = 0; i < width && line + i < kSamplesPerGranule; ++i) {
        const float mid = spectrum_[0][line + i];
        const float sideValue = spectrum_[1][line + i];
        spectrum_[0][line + i] = (mid + sideValue) * kInverseRootTwo;
        spectrum_[1][line + i] = (mid - sideValue) * kInverseRootTwo;
      }
    }
    line += width;
  }
}


// Short-block coefficients arrive grouped by window, but the filterbank wants them interleaved by
// frequency, six lines per window per subband. The destination formula below does that shuffle.
void Decoder::reorderShortBlocks(const FrameHeader& header, const GranuleInfo& granule,
                                 int channel) {
  if (granule.blockType != 2) return;

  const uint8_t* widths = bandWidths(header, granule);
  float scratch[kSamplesPerGranule];
  std::memcpy(scratch, spectrum_[channel], sizeof(scratch));

  const int longLines = granule.mixedBlock ? 36 : 0;
  int source = longLines;
  int band = granule.mixedBlock ? 8 : 0;
  int frequency = longLines / 3;

  for (; widths[band] != 0 && source < kSamplesPerGranule; band += 3) {
    const int width = widths[band];
    for (int window = 0; window < 3; ++window) {
      for (int line = 0; line < width && source < kSamplesPerGranule; ++line, ++source) {
        const int position = frequency + line;
        const int destination = (position / 6) * 18 + window * 6 + (position % 6);
        if (destination < kSamplesPerGranule) scratch[destination] = spectrum_[channel][source];
      }
    }
    frequency += width;
  }
  std::memcpy(spectrum_[channel], scratch, sizeof(scratch));
}

// Butterflies over the 8 lines either side of each subband boundary, cancelling the aliasing the
// encoder's hybrid filterbank left behind. Pure short blocks have no boundary to fix.
void Decoder::reduceAliasing(const GranuleInfo& granule, int channel) {
  if (granule.blockType == 2 && !granule.mixedBlock) return;
  const int boundaries = (granule.blockType == 2 && granule.mixedBlock) ? 1 : kSubbands - 1;

  float* x = spectrum_[channel];
  for (int boundary = 0; boundary < boundaries; ++boundary) {
    const int centre = 18 * (boundary + 1);
    for (int i = 0; i < 8; ++i) {
      const float below = x[centre - 1 - i];
      const float above = x[centre + i];
      x[centre - 1 - i] = below * kAliasCs[i] - above * kAliasCa[i];
      x[centre + i] = above * kAliasCs[i] + below * kAliasCa[i];
    }
  }
}


void Decoder::inverseMdct(const GranuleInfo& granule, int channel) {
  const CosineTables& table = cosines();
  float* x = spectrum_[channel];

  for (int subband = 0; subband < kSubbands; ++subband) {
    float* input = x + subband * kSamplesPerSubband;

    // Upper subbands are usually zero above some line, so the trailing-zero count below picks the
    // cheapest IMDCT width. A fully silent subband only needs the overlap tail flushed.
    int lines = kSamplesPerSubband;
    while (lines > 0 && input[lines - 1] == 0.0f) --lines;

    if (lines == 0) {
      for (int i = 0; i < 18; ++i) {
        float value = overlap_[channel][subband][i];
        overlap_[channel][subband][i] = 0.0f;
        if ((subband & 1) && (i & 1)) value = -value;
        input[i] = value;
      }
      continue;
    }

    float output[36];

    const bool shortBlock =
        granule.blockType == 2 && !(granule.mixedBlock && subband < 2);
    if (shortBlock) {
      std::memset(output, 0, sizeof(output));
      for (int window = 0; window < 3; ++window) {
        const float* piece = input + window * 6;
        bool silent = true;
        for (int k = 0; k < 6; ++k) {
          if (piece[k] != 0.0f) {
            silent = false;
            break;
          }
        }
        if (silent) continue;

        float raw[12];
        for (int i = 0; i < 6; ++i) {
          float sum = 0.0f;
          for (int k = 0; k < 6; ++k) sum += piece[k] * table.imdct12[i][k];
          raw[3 + i] = sum;
        }
        for (int n = 0; n < 3; ++n) raw[2 - n] = -raw[3 + n];
        for (int n = 0; n < 3; ++n) raw[9 + n] = raw[8 - n];
        for (int i = 0; i < 12; ++i)
          output[6 + window * 6 + i] += raw[i] * kBlockWindow[2][i];
      }
    } else {
      const int windowType = (granule.mixedBlock && subband < 2) ? 0 : granule.blockType;
      if (lines <= 6)
        imdctRows<6>(input, table.imdct36, output);
      else if (lines <= 12)
        imdctRows<12>(input, table.imdct36, output);
      else
        imdctRows<18>(input, table.imdct36, output);
      for (int n = 0; n < 9; ++n) output[8 - n] = -output[9 + n];
      for (int n = 0; n < 9; ++n) output[27 + n] = output[26 - n];
      for (int i = 0; i < 36; ++i) output[i] *= kBlockWindow[windowType][i];
    }

    // Overlap-add against the previous granule's tail, then apply the frequency inversion the
    // polyphase synthesis expects: negate every second sample of every odd subband.
    for (int i = 0; i < 18; ++i) {
      float value = output[i] + overlap_[channel][subband][i];
      overlap_[channel][subband][i] = output[i + 18];
      if ((subband & 1) && (i & 1)) value = -value;
      input[i] = value;
    }
  }
}


void Decoder::synthesise(int channel, int granule, int16_t* pcm, int channels) {
  const CosineTables& table = cosines();
  float* history = synthesis_[channel];
  const float* x = spectrum_[channel];

  for (int slot = 0; slot < kSamplesPerSubband; ++slot) {
    // The write head walks backwards 64 entries per slot through the 1024-entry ring, so the
    // windowed taps below see a sliding 512-sample history without ever moving memory.
    const int offset = (synthesisOffset_[channel] - 64) & 1023;
    synthesisOffset_[channel] = offset;

    float sample[kSubbands];
    for (int band = 0; band < kSubbands; ++band)
      sample[band] = x[band * kSamplesPerSubband + slot];

    float basis[33];
    const float* halfSec[5] = {table.dctSec32, table.dctSec16, table.dctSec8, table.dctSec4,
                               table.dctSec2};
    dctLee<32>(sample, basis, halfSec);
    basis[32] = 0.0f;

    for (int i = 0; i < 64; ++i) {
      const int m = 16 + i;
      history[(offset + i) & 1023] =
          m <= 32 ? basis[m] : (m <= 64 ? -basis[64 - m] : -basis[m - 64]);
    }

    const float* window = table.window512;
    for (int j = 0; j < 32; ++j) {
      float s0 = 0.0f, s1 = 0.0f, s2 = 0.0f, s3 = 0.0f;
      for (int i = 0; i < 8; i += 2) {
        s0 += history[(offset + i * 128 + j) & 1023] * window[i * 64 + j];
        s1 += history[(offset + i * 128 + 96 + j) & 1023] * window[i * 64 + 32 + j];
        s2 += history[(offset + (i + 1) * 128 + j) & 1023] * window[(i + 1) * 64 + j];
        s3 += history[(offset + (i + 1) * 128 + 96 + j) & 1023] * window[(i + 1) * 64 + 32 + j];
      }
      const float sum = (s0 + s1) + (s2 + s3);
      const int index = (granule * kSamplesPerSubband + slot) * 32 + j;
      const float scaled = sum * 32768.0f;
      const int32_t clamped = scaled > 32767.0f    ? 32767
                              : scaled < -32768.0f ? -32768
                                                   : static_cast<int32_t>(scaled);
      pcm[index * channels + channel] = static_cast<int16_t>(clamped);
    }
  }
}


bool Decoder::decodeGranule(const FrameHeader& header, const SideInfo& side, int granule,
                            BitReader& bits, int16_t* pcm) {
  const int channels = header.channels();
  int coefficients[kSamplesPerGranule];

  for (int channel = 0; channel < channels; ++channel) {
    const GranuleInfo& g = side.granules[granule][channel];
    const std::size_t granuleStart = bits.bitPos();

    if (granule == 0 || g.blockType == 2)
      std::memset(scalefactors_[channel], 0, sizeof(scalefactors_[channel]));
    const int scalefactorBits = readScalefactors(bits, side, granule, channel,
                                                 scalefactors_[channel]);
    const std::size_t huffmanEnd = granuleStart + g.part2_3Length;
    if (scalefactorBits > g.part2_3Length) return false;

    if (!readSpectrum(bits, header, g, huffmanEnd, coefficients)) return false;
    requantize(header, g, coefficients, channel);

    // The Huffman data may stop short of its budget or trail padding bits; part2_3_length is what
    // decides where the next channel starts.
    bits.seekBits(huffmanEnd);
  }

  applyStereo(header, side, granule);
  for (int channel = 0; channel < channels; ++channel) {
    const GranuleInfo& g = side.granules[granule][channel];
    reorderShortBlocks(header, g, channel);
    reduceAliasing(g, channel);
    inverseMdct(g, channel);
    synthesise(channel, granule, pcm, channels);
  }
  return true;
}

DecodeResult Decoder::decode(const uint8_t* data, std::size_t bytes, int16_t* pcm) {
  DecodeResult result;
  if (data == nullptr || bytes < 4) return result;

  std::size_t at = 0;
  if (!findSync(data, bytes, 0, at)) {
    result.status = DecodeStatus::NoSync;
    result.bytesConsumed = bytes > 3 ? bytes - 3 : 0;
    return result;
  }

  FrameHeader header{};
  if (!parseHeader(data + at, bytes - at, header)) {
    result.bytesConsumed = at + 1;
    result.status = DecodeStatus::NoSync;
    return result;
  }

  const int frameBytes = header.frameBytes();
  if (frameBytes <= 0) {
    result.status = DecodeStatus::Unsupported;
    result.bytesConsumed = at + 4;
    return result;
  }
  if (at + static_cast<std::size_t>(frameBytes) > bytes) {
    result.status = DecodeStatus::NeedMoreData;
    result.bytesConsumed = at;
    return result;
  }

  result.bytesConsumed = at + frameBytes;
  if (!isSupported(header) || rateIndex(header.sampleRateHz) < 0) {
    result.status = DecodeStatus::Unsupported;
    return result;
  }

  const uint8_t* frame = data + at;
  int offset = 4 + (header.hasCrc ? 2 : 0);
  const int sideBytes = sideInfoBytes(header);
  if (offset + sideBytes > frameBytes) {
    result.status = DecodeStatus::CorruptFrame;
    return result;
  }

  BitReader sideBits(frame + offset, sideBytes);
  SideInfo side{};
  if (!parseSideInfo(sideBits, header, side)) {
    result.status = DecodeStatus::CorruptFrame;
    return result;
  }
  offset += sideBytes;

  // The frame's main data starts main_data_begin bytes before this frame. If we have not banked
  // that much yet (fresh stream, or just after a seek) this frame is undecodable: bank and skip.
  const std::size_t mainDataBytes = static_cast<std::size_t>(frameBytes - offset);
  if (static_cast<std::size_t>(side.mainDataBegin) > reservoirFill_) {
    if (mainDataBytes <= sizeof(reservoir_)) {
      if (reservoirFill_ + mainDataBytes > kReservoirBytes) {
        const std::size_t keep =
            mainDataBytes >= kReservoirBytes ? 0 : kReservoirBytes - mainDataBytes;
        std::memmove(reservoir_, reservoir_ + reservoirFill_ - keep, keep);
        reservoirFill_ = keep;
      }
      std::memcpy(reservoir_ + reservoirFill_, frame + offset, mainDataBytes);
      reservoirFill_ += mainDataBytes;
    }
    result.status = DecodeStatus::NeedMoreData;
    return result;
  }

  std::size_t start = reservoirFill_ - side.mainDataBegin;
  if (reservoirFill_ + mainDataBytes > sizeof(reservoir_)) {
    result.status = DecodeStatus::CorruptFrame;
    return result;
  }
  std::memcpy(reservoir_ + reservoirFill_, frame + offset, mainDataBytes);
  const std::size_t available = reservoirFill_ + mainDataBytes;

  BitReader bits(reservoir_ + start, available - start);
  bool ok = true;
  for (int granule = 0; granule < kGranules && ok; ++granule)
    ok = decodeGranule(header, side, granule, bits, pcm);

  // Keep only the last 511 bytes; that is as far back as any following frame can reach.
  const std::size_t keep = available > kReservoirBytes ? kReservoirBytes : available;
  std::memmove(reservoir_, reservoir_ + available - keep, keep);
  reservoirFill_ = keep;

  if (!ok) {
    result.status = DecodeStatus::CorruptFrame;
    return result;
  }

  result.status = DecodeStatus::Ok;
  result.sampleRateHz = header.sampleRateHz;
  result.channels = header.channels();
  result.samples = header.samplesPerFrame();
  return result;
}

}
}
