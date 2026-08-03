#pragma once

#include "core/render/Canvas.h"
#include "core/render/MatrixLayout.h"

namespace awtrix {

// Built-in apps lay out inside a fixed 32 px frame centred on the matrix, so a wider panel just
// gains a margin instead of stretching the layout.
inline constexpr int kContentFrameWidth = kMatrixWidthMin;

struct ContentFrame {
  int x = 0;
  int width = kContentFrameWidth;
};

inline ContentFrame contentFrame(const Canvas& c, int contentWidth = 0) {
  ContentFrame f;
  f.width = contentWidth > kContentFrameWidth ? contentWidth : kContentFrameWidth;
  f.x = (c.width() - f.width) / 2;
  if (f.x < 0) f.x = 0;
  return f;
}

}
