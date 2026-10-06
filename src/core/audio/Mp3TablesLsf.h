// Layer III tables for MPEG-2 and MPEG-2.5, the half and quarter sample rates. The scalefactor
// band widths are ISO/IEC 13818-3 Table B.2 (MPEG-2.5 reuses the 16 kHz rows at 11.025 and
// 12 kHz), the partitions Table 2.4.3.2 of the same standard. Not generated: the annex that
// tools/gen_mp3_spec_tables.py reads is the MPEG-1 one. The static_asserts below check that every
// row covers the spectrum; test_mp3pcm checks the decode against ffmpeg at each rate.
//
// Same layout as Mp3Tables.h: a linear walk covers all 576 lines, short blocks repeat each width
// once per window, and each list ends with a 0.
#pragma once

#include <cstddef>
#include <cstdint>

namespace awtrix {
namespace mp3 {

// Row index into the tables below, or -1.
inline constexpr int lsfRateIndex(int hz) {
  return hz == 22050 ? 0
         : hz == 24000 ? 1
         : (hz == 16000 || hz == 11025 || hz == 12000) ? 2
         : hz == 8000 ? 3
                      : -1;
}

inline constexpr uint8_t kLsfBandsLong[4][23] = {
    {6, 6, 6, 6, 6, 6, 8, 10, 12, 14, 16, 20, 24, 28, 32, 38, 46, 52, 60, 68, 58, 54, 0},  // 22050 Hz
    {6, 6, 6, 6, 6, 6, 8, 10, 12, 14, 16, 18, 22, 26, 32, 38, 46, 54, 62, 70, 76, 36, 0},  // 24000 Hz
    {6, 6, 6, 6, 6, 6, 8, 10, 12, 14, 16, 20, 24, 28, 32, 38, 46, 52, 60, 68, 58, 54, 0},  // 16000 Hz
    {12, 12, 12, 12, 12, 12, 16, 20, 24, 28, 32, 40, 48, 56, 64, 76, 90, 2, 2, 2, 2, 2, 0},  // 8000 Hz
};

inline constexpr uint8_t kLsfBandsShort[4][40] = {
    {4, 4, 4, 4, 4, 4, 4, 4, 4, 6, 6, 6, 6, 6, 6, 8, 8, 8, 10, 10, 10, 14, 14, 14, 18, 18, 18, 26, 26, 26, 32, 32, 32, 42, 42, 42, 18, 18, 18, 0},  // 22050 Hz
    {4, 4, 4, 4, 4, 4, 4, 4, 4, 6, 6, 6, 8, 8, 8, 10, 10, 10, 12, 12, 12, 14, 14, 14, 18, 18, 18, 24, 24, 24, 32, 32, 32, 44, 44, 44, 12, 12, 12, 0},  // 24000 Hz
    {4, 4, 4, 4, 4, 4, 4, 4, 4, 6, 6, 6, 8, 8, 8, 10, 10, 10, 12, 12, 12, 14, 14, 14, 18, 18, 18, 24, 24, 24, 30, 30, 30, 40, 40, 40, 18, 18, 18, 0},  // 16000 Hz
    {8, 8, 8, 8, 8, 8, 8, 8, 8, 12, 12, 12, 16, 16, 16, 20, 20, 20, 24, 24, 24, 28, 28, 28, 36, 36, 36, 2, 2, 2, 2, 2, 2, 2, 2, 2, 26, 26, 26, 0},  // 8000 Hz
};

// Mixed blocks: six long bands, then the short bands from the fourth on. Both halves meet at the
// same line, 36 or at 8 kHz 72.
inline constexpr uint8_t kLsfBandsMixed[4][37] = {
    {6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 8, 8, 8, 10, 10, 10, 14, 14, 14, 18, 18, 18, 26, 26, 26, 32, 32, 32, 42, 42, 42, 18, 18, 18, 0},  // 22050 Hz
    {6, 6, 6, 6, 6, 6, 6, 6, 6, 8, 8, 8, 10, 10, 10, 12, 12, 12, 14, 14, 14, 18, 18, 18, 24, 24, 24, 32, 32, 32, 44, 44, 44, 12, 12, 12, 0},  // 24000 Hz
    {6, 6, 6, 6, 6, 6, 6, 6, 6, 8, 8, 8, 10, 10, 10, 12, 12, 12, 14, 14, 14, 18, 18, 18, 24, 24, 24, 30, 30, 30, 40, 40, 40, 18, 18, 18, 0},  // 16000 Hz
    {12, 12, 12, 12, 12, 12, 12, 12, 12, 16, 16, 16, 20, 20, 20, 24, 24, 24, 28, 28, 28, 36, 36, 36, 2, 2, 2, 2, 2, 2, 2, 2, 2, 26, 26, 26, 0},  // 8000 Hz
};

inline constexpr int kLsfMixedLongBands = 6;

// How many scalefactors each of the four partitions holds, by the scalefac_compress range (rows
// 0-2 plain, 3-5 intensity-stereo positions) and by block kind (long, short, mixed). A short or
// mixed count is in band-windows.
inline constexpr uint8_t kLsfPartitions[6][3][4] = {
    {{6, 5, 5, 5}, {9, 9, 9, 9}, {6, 9, 9, 9}},
    {{6, 5, 7, 3}, {9, 9, 12, 6}, {6, 9, 12, 6}},
    {{11, 10, 0, 0}, {18, 18, 0, 0}, {15, 18, 0, 0}},
    {{7, 7, 7, 0}, {12, 12, 12, 0}, {6, 15, 12, 0}},
    {{6, 6, 6, 3}, {12, 9, 9, 6}, {6, 12, 9, 6}},
    {{8, 8, 5, 0}, {15, 12, 9, 0}, {6, 18, 9, 0}},
};

namespace lsfcheck {
template <std::size_t Width>
constexpr bool rowsCover(const uint8_t (&rows)[4][Width]) {
  for (std::size_t row = 0; row < 4; ++row) {
    int total = 0;
    for (std::size_t i = 0; i < Width; ++i) total += rows[row][i];
    if (total != 576 || rows[row][Width - 1] != 0) return false;
  }
  return true;
}
constexpr bool partitionsCount() {
  const int kinds[3] = {21, 36, 33};
  for (int row = 0; row < 6; ++row)
    for (int k = 0; k < 3; ++k)
      if (kLsfPartitions[row][k][0] + kLsfPartitions[row][k][1] + kLsfPartitions[row][k][2] +
              kLsfPartitions[row][k][3] !=
          kinds[k])
        return false;
  return true;
}
}  // namespace lsfcheck

static_assert(lsfcheck::rowsCover(kLsfBandsLong), "an MPEG-2 long band row misses lines");
static_assert(lsfcheck::rowsCover(kLsfBandsShort), "an MPEG-2 short band row misses lines");
static_assert(lsfcheck::rowsCover(kLsfBandsMixed), "an MPEG-2 mixed band row misses lines");
static_assert(lsfcheck::partitionsCount(),
              "MPEG-2 partitions must hold 21 long, 36 short or 33 mixed scalefactors");

}  // namespace mp3
}  // namespace awtrix
