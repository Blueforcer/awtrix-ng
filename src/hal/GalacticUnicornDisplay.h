#pragma once

// Display stream layout adapted from Pimoroni pico v1.23.0 (MIT).
// See THIRD-PARTY-NOTICES.md. No SDK/Arduino dependencies: host-testable.
#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include "core/render/Canvas.h"
#include "core/render/Color.h"
#include "core/render/ColorGrade.h"

namespace awtrix::galactic {
inline constexpr int Width = 53, Height = 11, Planes = 14;
inline constexpr size_t PlaneBytes = 60, RowBytes = Planes * PlaneBytes;
inline constexpr size_t StreamBytes = Height * RowBytes;
inline constexpr uint8_t ColumnClock = 13, ColumnData = 14, ColumnLatch = 15, ColumnBlank = 16;
inline constexpr uint8_t Row0 = 17, Row1 = 18, Row2 = 19, Row3 = 20;
inline constexpr uint8_t LightSensor = 28, LightAdc = 2;
inline constexpr uint8_t ButtonA = 0, ButtonB = 1, ButtonC = 3, ButtonD = 6;
inline constexpr uint8_t Sleep = 27, VolumeUp = 7, VolumeDown = 8;
inline constexpr uint8_t BrightnessUp = 21, BrightnessDown = 26;
enum class InputAction { Left, Select, Right, None, Power, VolumeUp, VolumeDown, BrightnessUp, BrightnessDown };
struct Input { uint8_t pin; InputAction action; };
inline constexpr std::array<Input, 9> Inputs{{
    {ButtonA, InputAction::Left}, {ButtonB, InputAction::Select},
    {ButtonC, InputAction::Right}, {ButtonD, InputAction::None},
    {Sleep, InputAction::Power}, {VolumeUp, InputAction::VolumeUp},
    {VolumeDown, InputAction::VolumeDown}, {BrightnessUp, InputAction::BrightnessUp},
    {BrightnessDown, InputAction::BrightnessDown}}};
class DebouncedButton {
 public:
  // Active-low conversion belongs to the GPIO adapter. True means pressed here.
  bool update(bool pressed, int64_t nowMs) {
    if (pressed != raw_) { raw_ = pressed; changedMs_ = nowMs; }
    const bool previous = stable_;
    if (nowMs - changedMs_ >= 35) stable_ = raw_;
    return stable_ && !previous;
  }
  bool pressed() const { return stable_; }
 private:
  bool raw_ = false, stable_ = false;
  int64_t changedMs_ = 0;
};
inline int stepClamped(int value, int delta, int maximum) {
  const int64_t result = static_cast<int64_t>(value) + delta;
  return result < 0 ? 0 : result > maximum ? maximum : static_cast<int>(result);
}
inline constexpr int sanitizeHeight(int h) { return h == 8 ? 8 : 11; }
// Physical, top-down panel row, before the wiring's X/Y reversal.
inline constexpr int physicalRow(int y, int height) {
  return y < 0 || y >= sanitizeHeight(height) ? -1 : y + (height == 8 ? 1 : 0);
}
struct alignas(4) Stream { std::array<uint8_t, StreamBytes> bytes{}; };
inline void clearStream(Stream& s) {
  s.bytes.fill(0);
  for (int row = 0; row < Height; ++row) {
    for (int plane = 0; plane < Planes; ++plane) {
      auto* p = s.bytes.data() + row * RowBytes + plane * PlaneBytes;
      p[0] = Width - 1;
      p[1] = row;
      const uint32_t ticks = 1u << plane;
      for (int b = 0; b < 4; ++b) p[56 + b] = ticks >> (8 * b);
    }
  }
}
inline void packPixel(Stream& s, int x, int physicalY, uint16_t r, uint16_t g, uint16_t b) {
  if (x < 0 || x >= Width || physicalY < 0 || physicalY >= Height) return;
  const size_t offset = (Height - 1 - physicalY) * RowBytes + 2 + (Width - 1 - x);
  for (int plane = 0; plane < Planes; ++plane)
    s.bytes[offset + plane * PlaneBytes] = (((r >> plane) & 1u) << 2) |
                                         (((g >> plane) & 1u) << 1) | ((b >> plane) & 1u);
}
// NG ColorGrade semantics, with a 14-bit LUT rather than an 8-bit final canvas.
// Saturation first, then gamma ONCE, then linear-light brightness/correction/tint.
// Upstream's fixed gamma 2.2 is replaced with the NG gamma setting (default 1.9).
class Grade14 {
 public:
  Grade14() { rebuild(); }
  void setParams(const render::GradeParams& p) {
    if (p == params_) return;
    params_ = p;
    rebuild();
  }
  void pack(Stream& s, int x, int y, uint32_t rgb) const {
    const auto c = color::desaturate(rgb, params_.saturation);
    packPixel(s, x, y, lut_[0][color::red(c)], lut_[1][color::green(c)], lut_[2][color::blue(c)]);
  }
  uint16_t channel(int ch, uint8_t value) const { return lut_[ch][value]; }
 private:
  void rebuild() {
    const uint8_t scales[] = {
      color::scale8(color::red(params_.correction), color::red(params_.tint)),
      color::scale8(color::green(params_.correction), color::green(params_.tint)),
      color::scale8(color::blue(params_.correction), color::blue(params_.tint))};
    for (int i = 0; i < 256; ++i) {
      const float f = i / 255.0f;
      const auto gamma = static_cast<uint32_t>(std::lround(
          (params_.gamma > 0 ? std::pow(f, params_.gamma) : f) * 16383.0f));
      for (int ch = 0; ch < 3; ++ch)
        lut_[ch][i] = gamma * params_.brightness / 255u * scales[ch] / 255u;
    }
  }
  render::GradeParams params_;
  uint16_t lut_[3][256]{};
};
// The wiring is fixed, but mirror and rotate180 are drawing transforms, applied as MatrixLayout does.
struct Orientation {
  bool mirror = false;
  bool rotate180 = false;
};
inline void packCanvas(Stream& stream, const Canvas& canvas, int height, const Grade14& grade,
                       Orientation orientation = {}) {
  clearStream(stream); // Also clears letterbox rows and stale pixels in a smaller canvas.
  const int w = std::min(Width, canvas.width());
  const int h = std::min(sanitizeHeight(height), canvas.height());
  for (int y = 0; y < h; ++y)
    for (int x = 0; x < w; ++x) {
      int px = orientation.mirror ? w - 1 - x : x, py = y;
      if (orientation.rotate180) {
        px = w - 1 - px;
        py = h - 1 - py;
      }
      grade.pack(stream, px, physicalRow(py, height), canvas.getPixel(x, y));
    }
}
}
