#pragma once

#include <cstdint>

#include "core/render/Canvas.h"

namespace awtrix {

struct Settings;

namespace render {

// Panel calibration applied to the finished frame: saturation is a percentage (100 = untouched),
// correction and tint are 0xRRGGBB per-channel multipliers.
struct GradeParams {
  int saturation = 100;
  float gamma = 1.9f;
  uint32_t correction = 0xFFFFFFu;
  uint32_t tint = 0xFFFFFFu;

  bool operator==(const GradeParams& o) const {
    return saturation == o.saturation && gamma == o.gamma && correction == o.correction &&
           tint == o.tint;
  }
  bool operator!=(const GradeParams& o) const { return !(*this == o); }
};

class ColorGrade {
 public:
  ColorGrade() { rebuild(); }

  void setParams(const GradeParams& p);
  const GradeParams& params() const { return params_; }
  bool isIdentity() const { return identity_; }

  uint32_t applyPixel(uint32_t c) const;
  void apply(const Canvas& src, Canvas& dst) const;

 private:
  void rebuild();

  GradeParams params_;
  uint8_t lut_[3][256];
  bool identity_ = true;
};

GradeParams gradeFrom(const Settings& s);

}
}
