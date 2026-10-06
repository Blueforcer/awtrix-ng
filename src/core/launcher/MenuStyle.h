#pragma once

#include "core/render/Canvas.h"

namespace awtrix::launcher::style {

inline constexpr uint32_t kAmber = 0xFFC040u;
inline constexpr uint32_t kCyan = 0x00C8FFu;
inline constexpr uint32_t kTrack = 0x1E1E1Eu;
inline constexpr float kDim = 0.28f;

inline float selectionLight(int index, int selected, int previous, float fade, bool fadePrevious = true) {
  if (index == selected) return kDim + (1.0f - kDim) * fade;
  if (index == previous && fadePrevious) return 1.0f - (1.0f - kDim) * fade;
  return kDim;
}

template <int N>
void sprite(Canvas& canvas, int x, int y, const char* const (&rows)[N], uint32_t rgb) {
  for (int j = 0; j < N; ++j)
    for (int i = 0; rows[j][i]; ++i)
      if (rows[j][i] == '#') canvas.setPixel(x + i, y + j, rgb);
}

}
