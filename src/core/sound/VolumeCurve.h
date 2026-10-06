#ifndef AWTRIX_CORE_SOUND_VOLUME_CURVE_H
#define AWTRIX_CORE_SOUND_VOLUME_CURVE_H

/* How loud a volume in percent plays: about 35*log10(40v)-60 dB, from -60 dB at 0 % to -3 dB at
 * 100 %, so equal steps of the slider sound like equal steps. The TC002 speaker sets its gain from
 * it; the ESP32-S3 scales its samples by it. Usable from C. */

#include <stdint.h>

static inline int8_t awtrix_volume_db(uint8_t percent) {
  static const int8_t table[101] = {
      -60, -60, -60, -57, -52, -49, -46, -44, -42, -40, -38, -37, -36, -34, -33, -32, -31,
      -30, -29, -29, -28, -27, -26, -26, -25, -25, -24, -23, -23, -22, -22, -21, -21, -20,
      -20, -19, -19, -19, -18, -18, -17, -17, -17, -16, -16, -16, -15, -15, -15, -14, -14,
      -14, -13, -13, -13, -13, -12, -12, -12, -11, -11, -11, -11, -10, -10, -10, -10, -10,
      -9,  -9,  -9,  -9,  -8,  -8,  -8,  -8,  -8,  -7,  -7,  -7,  -7,  -7,  -6,  -6,  -6,
      -6,  -6,  -6,  -5,  -5,  -5,  -5,  -5,  -5,  -4,  -4,  -4,  -4,  -4,  -4,  -3};
  return table[percent > 100 ? 100 : percent];
}

#ifdef __cplusplus
#include <cmath>

namespace awtrix {
namespace sound {

constexpr int32_t kVolumeUnity = 32768;

// The Q15 scale that makes `volume` percent sound as the curve has it while the output plays at
// `reference` percent: kVolumeUnity at or above the reference, 0 at 0 %.
inline int32_t volumeGain(uint8_t volume, uint8_t reference) {
  if (!volume) return 0;
  if (volume >= reference) return kVolumeUnity;
  const int db = awtrix_volume_db(volume) - awtrix_volume_db(reference);
  return static_cast<int32_t>(std::lround(kVolumeUnity * std::pow(10.0, db / 20.0)));
}

}
}
#endif

#endif
