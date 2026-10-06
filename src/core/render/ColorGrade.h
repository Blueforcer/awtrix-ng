#pragma once

#include <cstdint>

#include "core/render/Canvas.h"
#include "core/render/OutputTable.h"
#include "platform_render/OutputGrade.h"

namespace awtrix {

struct Settings;

namespace render {

// Panel calibration applied to the finished frame: saturation is a percentage (100 = untouched),
// correction and tint are 0xRRGGBB per-channel multipliers, brightness is the level to show it at.
struct GradeParams {
  int saturation = 100;
  float gamma = 1.9f;
  uint32_t correction = 0xFFFFFFu;
  uint32_t tint = 0xFFFFFFu;
  uint8_t brightness = 255;

  bool operator==(const GradeParams& o) const {
    return saturation == o.saturation && gamma == o.gamma && correction == o.correction &&
           tint == o.tint && brightness == o.brightness;
  }
  bool operator!=(const GradeParams& o) const { return !(*this == o); }
};

class ColorGrade : private OutputGrade {
 public:
  ColorGrade() { rebuild(); }

  void setParams(const GradeParams& p);
  // Calibration changes keep the driver's current brightness.
  void setGrade(const GradeParams& p) {
    GradeParams next = p;
    next.brightness = params_.brightness;
    setParams(next);
  }
  void setBrightness(uint8_t brightness) {
    if (params_.brightness == brightness) return;
    params_.brightness = brightness;
    rebuild();
  }
  const GradeParams& params() const { return params_; }
  // Platform output calibration; the table must outlive the grade.
  void setOutput(const OutputTable* table);
  bool isIdentity() const { return identity_; }

  uint32_t applyPixel(uint32_t c) const;
  void apply(const Canvas& src, Canvas& dst) const;

 private:
  void rebuild();
  void buildGammaTable();

  GradeParams params_;
  uint8_t lut_[3][256];
  // 16-bit curve, quantised only when it becomes an output code.
  uint16_t gamma16_[256];
  float gammaBuilt_ = -1.0f;
  bool identity_ = true;
};

GradeParams gradeFrom(const Settings& s);

}
}
