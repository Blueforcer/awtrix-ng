#pragma once

#include <cstdint>

#include "core/render/Canvas.h"
#include "core/render/ColorGrade.h"
#include "core/render/MatrixLayout.h"

namespace awtrix {

class MatrixRenderer {
 public:
  bool begin(int pin, const MatrixLayout& layout, uint8_t brightness);
  void setLayout(const MatrixLayout& layout) {
    if (!ledsAllocated_ || (layout.width() == layout_.width() && layout.height() == layout_.height()))
      layout_ = layout;
  }
  bool ready() const { return ledsAllocated_ > 0; }
  void setBrightness(uint8_t brightness);
  void setGrade(const render::GradeParams& grade) { grade_.setGrade(grade); }
  void show(const Canvas& canvas);

 private:
  int xyToIndex(int x, int y) const;
  MatrixLayout layout_;
  int ledsAllocated_ = 0;
  render::ColorGrade grade_;
};

}
