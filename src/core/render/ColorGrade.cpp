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

// A lit channel never decodes to nothing here; whether it survives the brightness scale afterwards
// is the panel running out of levels, not the curve throwing them away.
void ColorGrade::buildGammaTable() {
  if (params_.gamma == gammaBuilt_) return;
  gammaBuilt_ = params_.gamma;
  gamma16_[0] = 0;
  for (int i = 1; i < 256; ++i) {
    const float f = params_.gamma > 0.0f ? std::pow(i / 255.0f, params_.gamma) : i / 255.0f;
    long v = std::lround(f * 65535.0f);
    if (v < 1) v = 1;
    if (v > 65535) v = 65535;
    gamma16_[i] = static_cast<uint16_t>(v);
  }
}

// Bakes gamma, brightness and correction*tint into one lookup table per channel. Saturation stays
// per-pixel: it mixes the channels and is not a per-channel curve.
//
// Brightness and the channel balance both scale the curve's result, not its input - they are
// linear-light quantities. Scaling first would make the emitted light go as brightness^gamma, which
// puts the bottom of the control range below one PWM step.
void ColorGrade::rebuild() {
  buildGammaTable();

  const uint8_t scale[3] = {
      color::scaleChannel8(color::red(params_.correction), color::red(params_.tint)),
      color::scaleChannel8(color::green(params_.correction), color::green(params_.tint)),
      color::scaleChannel8(color::blue(params_.correction), color::blue(params_.tint)),
  };

  prepareOutput();
  identity_ = params_.saturation >= 100;
  for (int i = 0; i < 256; ++i) {
    const uint32_t lit = static_cast<uint32_t>(gamma16_[i]) * params_.brightness / 255u;
    for (int ch = 0; ch < 3; ++ch) {
      const uint32_t light = lit * scale[ch] / 255u;
      rememberOutput(ch, i, static_cast<uint16_t>(gamma16_[i] * scale[ch] / 255u));
      lut_[ch][i] = encodeOutput(light);
      if (lut_[ch][i] != static_cast<uint8_t>(i)) identity_ = false;
    }
  }
}

uint32_t ColorGrade::applyPixel(uint32_t c) const {
  if (identity_) return c;
  const uint32_t s = color::desaturate(c, params_.saturation);
  const uint8_t r = color::red(s), g = color::green(s), b = color::blue(s);
  uint8_t codes[3] = {lut_[0][r], lut_[1][g], lut_[2][b]};
  settleOutput(params_.brightness, r, g, b, codes);
  return color::pack(codes[0], codes[1], codes[2]);
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
