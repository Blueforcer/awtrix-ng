#pragma once

#include <cstdint>

#include "core/render/Canvas.h"
#include "core/render/ColorGrade.h"
#include "core/render/MatrixLayout.h"

namespace awtrix {

class MatrixRenderer {
 public:
  void begin(int pin, const MatrixLayout& layout, uint8_t brightness);
  void setLayout(const MatrixLayout& layout) { layout_ = layout; }
  void setBrightness(uint8_t brightness);
  void setGrade(const render::GradeParams& grade) {
    base_ = grade;
    applyGrade();
  }
  void show(const Canvas& canvas);

 private:
  int xyToIndex(int x, int y) const;
  // Brightness is part of the grade, so either input has to rebuild the same lookup table.
  void applyGrade() {
    render::GradeParams p = base_;
    p.brightness = brightness_;
    grade_.setParams(p);
  }

  MatrixLayout layout_;
  int ledsAllocated_ = 0;
  render::GradeParams base_;
  uint8_t brightness_ = 255;
  render::ColorGrade grade_;
};

}
