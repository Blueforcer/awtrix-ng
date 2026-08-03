#include "core/render/ColorGrade.h"

#include <cmath>

#include "core/Settings.h"
#include "core/render/Color.h"

namespace awtrix {
namespace render {

void ColorGrade::setParams(const GradeParams& p) {
  if (p == params_) return;
  params_ = p;
  rebuild();
}

// Bakes gamma and correction*tint into one lookup table per channel. Saturation stays per-pixel
// because it mixes the channels together and cannot be expressed as a per-channel curve.
void ColorGrade::rebuild() {
  float gamma = params_.gamma;
  if (gamma <= 0.0f) gamma = 1.0f;

  const uint8_t scale[3] = {
      color::scale8(color::red(params_.correction), color::red(params_.tint)),
      color::scale8(color::green(params_.correction), color::green(params_.tint)),
      color::scale8(color::blue(params_.correction), color::blue(params_.tint)),
  };

  identity_ = params_.saturation >= 100;
  for (int i = 0; i < 256; ++i) {
    uint8_t g = static_cast<uint8_t>(std::lround(std::pow(i / 255.0f, gamma) * 255.0f));
    // Gamma must never extinguish a channel that was lit, or the dimmest text vanishes entirely.
    if (i > 0 && g == 0) g = 1;
    for (int ch = 0; ch < 3; ++ch) {
      lut_[ch][i] = color::scale8(g, scale[ch]);
      if (lut_[ch][i] != static_cast<uint8_t>(i)) identity_ = false;
    }
  }
}

uint32_t ColorGrade::applyPixel(uint32_t c) const {
  if (identity_) return c;
  const uint32_t s = color::desaturate(c, params_.saturation);
  return color::pack(lut_[0][color::red(s)], lut_[1][color::green(s)], lut_[2][color::blue(s)]);
}

void ColorGrade::apply(const Canvas& src, Canvas& dst) const {
  if (src.width() != dst.width() || src.height() != dst.height()) return;
  const uint32_t* in = src.data();
  uint32_t* out = dst.data();
  const std::size_t n = src.size();
  if (identity_) {
    for (std::size_t i = 0; i < n; ++i) out[i] = in[i];
    return;
  }
  for (std::size_t i = 0; i < n; ++i) out[i] = applyPixel(in[i]);
}

GradeParams gradeFrom(const Settings& s) {
  GradeParams p;
  p.saturation = s.saturation;
  p.gamma = s.gamma;
  p.correction = s.colorCorrection.valueOr(0xFFFFFFu);
  p.tint = s.colorTint.valueOr(0xFFFFFFu);
  return p;
}

}
}
