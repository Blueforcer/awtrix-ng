#pragma once

#include <cstdint>

#include <algorithm>
#include <cmath>

#include "core/effects/EffectNoise.h"
#include "core/effects/IEffect.h"
#include "core/effects/PlasmaField.h"
#include "core/render/Color.h"

namespace awtrix {

// Boilerplate for the small effects: id, rate and an out-of-line render(). The FIXED_COLOURS
// variant is for the ones that ignore the palette entirely.
#define AWTRIX_EFFECT(CLASS, NAME, RATE)                            \
  class CLASS : public IEffect {                                    \
   public:                                                          \
    const std::string& id() const override { return id_; }          \
    float rate() const override { return RATE; }                    \
    void render(Canvas& c, int64_t f) override;                        \
                                                                    \
   private:                                                         \
    std::string id_ = NAME;                                         \
  }

#define AWTRIX_EFFECT_FIXED_COLOURS(CLASS, NAME, RATE)              \
  class CLASS : public IEffect {                                    \
   public:                                                          \
    const std::string& id() const override { return id_; }          \
    float rate() const override { return RATE; }                    \
    void render(Canvas& c, int64_t f) override;                        \
    bool usesPalette() const override { return false; }             \
                                                                    \
   private:                                                         \
    std::string id_ = NAME;                                         \
  }

AWTRIX_EFFECT(MovingLineEffect, "MovingLine", rate::kSteady);
AWTRIX_EFFECT_FIXED_COLOURS(BrickBreakerEffect, "BrickBreaker", rate::kSteady);
AWTRIX_EFFECT_FIXED_COLOURS(PingPongEffect, "PingPong", rate::kSteady);
AWTRIX_EFFECT(RadarEffect, "Radar", rate::kContinuous);
AWTRIX_EFFECT(CheckerboardEffect, "Checkerboard", rate::kDrift);
AWTRIX_EFFECT(FireworksEffect, "Fireworks", rate::kSteady);
AWTRIX_EFFECT(PlasmaCloudEffect, "PlasmaCloud", rate::kContinuous);
AWTRIX_EFFECT(RippleEffect, "Ripple", rate::kSteady);
AWTRIX_EFFECT(SnakeEffect, "Snake", rate::kSteady);
AWTRIX_EFFECT(PacificaEffect, "Pacifica", rate::kContinuous);
AWTRIX_EFFECT_FIXED_COLOURS(MatrixEffect, "Matrix", rate::kSteady);
AWTRIX_EFFECT(SwirlInEffect, "SwirlIn", rate::kContinuous);
AWTRIX_EFFECT(SwirlOutEffect, "SwirlOut", rate::kContinuous);
AWTRIX_EFFECT_FIXED_COLOURS(LookingEyesEffect, "LookingEyes", rate::kContinuous);
AWTRIX_EFFECT(TwinklingStarsEffect, "TwinklingStars", rate::kSteady);
AWTRIX_EFFECT(ColorWavesEffect, "ColorWaves", rate::kContinuous);

#undef AWTRIX_EFFECT
#undef AWTRIX_EFFECT_FIXED_COLOURS

inline void MovingLineEffect::render(Canvas& c, int64_t f) {
  c.clear(0);
  const int x = static_cast<int>(f) % c.width();
  const uint32_t col = paletteColor(static_cast<uint8_t>(x * 255 / c.width()), 0x00AAFFu);
  for (int y = 0; y < c.height(); ++y) c.setPixel(x, y, col);
}

inline void BrickBreakerEffect::render(Canvas& c, int64_t f) {
  c.clear(0);
  for (int y = 0; y < 3; ++y)
    for (int x = 0; x < c.width(); x += 2) c.setPixel(x, y, color::fromHsv((x * 12) % 360, 100, 55));
  // Triangle wave across the width: count up to the far edge, then mirror back.
  int p = static_cast<int>(f) % (2 * (c.width() - 1));
  int bx = p < c.width() ? p : 2 * (c.width() - 1) - p;
  c.setPixel(bx, c.height() - 2, 0xFFFFFFu);
  c.fillRect(bx - 1, c.height() - 1, 3, 1, 0x888888u);
}

inline void PingPongEffect::render(Canvas& c, int64_t f) {
  c.clear(0);
  int px = static_cast<int>(f) % (2 * (c.width() - 1));
  int x = px < c.width() ? px : 2 * (c.width() - 1) - px;
  int py = static_cast<int>(f * 7 / 10) % (2 * (c.height() - 1));
  int y = py < c.height() ? py : 2 * (c.height() - 1) - py;
  c.setPixel(x, y, 0x00FF88u);
}

inline void RadarEffect::render(Canvas& c, int64_t f) {
  c.clear(0);
  const float a = f * kPhasePerStep;
  const int cx = c.width() / 2, cy = c.height() / 2;
  for (int r = 0; r < c.height(); ++r) {
    int x = cx + static_cast<int>(std::cos(a) * r);
    int y = cy + static_cast<int>(std::sin(a) * r);
    const uint32_t col = color::fromHsv(120, 100, 90 - r * 8 > 0 ? 90 - r * 8 : 10);
    c.setPixel(x, y, paletteColor(static_cast<uint8_t>(r * 255 / c.height()), col));
  }
}

inline void CheckerboardEffect::render(Canvas& c, int64_t f) {
  const int t = static_cast<int>(f % 2);
  for (int y = 0; y < c.height(); ++y)
    for (int x = 0; x < c.width(); ++x) {
      const bool on = ((x / 2 + y / 2) % 2) == t;
      const uint32_t col = paletteColor(static_cast<uint8_t>((x / 2 + y / 2) * 16), 0x2244AAu);
      c.setPixel(x, y, on ? col : 0x000000u);
    }
}

inline void FireworksEffect::render(Canvas& c, int64_t f) {
  c.clear(0);
  // One burst per 20 frames: roll fixes its position and colour, age drives the expanding ring.
  const int64_t burst = f / 20;
  const uint32_t roll = noise::hash2(static_cast<uint32_t>(burst), 0x46495245u);
  const int cx = 2 + static_cast<int>(roll % static_cast<uint32_t>(c.width() - 4));
  const int cy = 1 + static_cast<int>((roll >> 8) % static_cast<uint32_t>(c.height() - 3));
  const int age = static_cast<int>(f % 20);
  const int r = age / 3;
  uint32_t col = color::fromHsv(static_cast<int>((roll >> 16) % 360u), 100, age < 15 ? 80 : 20);
  col = paletteColor(static_cast<uint8_t>(roll >> 16), col);
  for (int deg = 0; deg < 360; deg += 45) {
    int x = cx + static_cast<int>(std::cos(deg * 3.14159f / 180) * r);
    int y = cy + static_cast<int>(std::sin(deg * 3.14159f / 180) * r);
    c.setPixel(x, y, col);
  }
}

inline void PlasmaCloudEffect::render(Canvas& c, int64_t f) {
  const float t = f * kPhasePerStep;
  const int w = c.width(), h = c.height();

  fx::Axes& a = fx::axes();
  const bool tabled = a.fits(w, h);
  if (tabled)
    fx::sampleAxes(a, w, h, [&](int x) { return std::sin(x * 0.2f + t); },
                   [&](int y) { return std::cos(y * 0.4f - t); },
                   [](int d) { return std::sin(d * 0.15f); });

  for (int y = 0; y < h; ++y)
    for (int x = 0; x < w; ++x) {
      const float v = tabled ? a.x[x] + a.y[y] + a.d[x + y]
                             : std::sin(x * 0.2f + t) + std::cos(y * 0.4f - t) +
                                   std::sin((x + y) * 0.15f);
      const float u = (v + 3) / 6;
      const uint8_t idx = static_cast<uint8_t>(u * 255);
      c.setPixel(x, y, paletteColorOr(idx, [u] {
                   return color::fromHsv((static_cast<int>(u * 120) + 180) % 360, 80, 45);
                 }));
    }
}

inline void RippleEffect::render(Canvas& c, int64_t f) {
  c.clear(0);
  const int cx = c.width() / 2, cy = c.height() / 2;
  const float rr = static_cast<float>(f % 12);
  for (int y = 0; y < c.height(); ++y)
    for (int x = 0; x < c.width(); ++x) {
      float d = std::sqrt(static_cast<float>((x - cx) * (x - cx) + (y - cy) * (y - cy)));
      if (std::fabs(d - rr) < 0.8f)
        c.setPixel(x, y, paletteColor(static_cast<uint8_t>(rr * 8), color::fromHsv(200, 100, 70)));
    }
}

inline void SnakeEffect::render(Canvas& c, int64_t f) {
  c.clear(0);
  const int len = 6;
  for (int i = 0; i < len; ++i) {
    int p = static_cast<int>(f) - i;
    if (p < 0) continue;
    // The snake crawls in reading order, dropping to the next row every width steps.
    int x = p % c.width();
    int row = (p / c.width()) % c.height();
    c.setPixel(x, row, paletteColor(static_cast<uint8_t>(i * 40), color::fromHsv(120, 100, 80 - i * 10)));
  }
}

inline void PacificaEffect::render(Canvas& c, int64_t f) {
  const float t = f * kPhasePerStep;
  const int cw = c.width(), ch = c.height();

  fx::Axes& a = fx::axes();
  const bool tabled = a.fits(cw, ch);
  if (tabled)
    fx::sampleAxes(a, cw, ch, [&](int x) { return std::sin(x * 0.3f + t); },
                   [&](int y) { return std::sin(y * 0.5f + t * 0.7f); }, [](int) { return 0.0f; });

  for (int y = 0; y < ch; ++y)
    for (int x = 0; x < cw; ++x) {
      const float w = tabled ? a.x[x] + a.y[y] : std::sin(x * 0.3f + t) + std::sin(y * 0.5f + t * 0.7f);
      const float u = (w + 2) / 4;
      const uint8_t idx = static_cast<uint8_t>(u * 255);
      c.setPixel(x, y, paletteColorOr(idx, [u] {
                   const int v = static_cast<int>(u * 120) + 20;
                   return color::fromRgb(0, v / 2, v);
                 }));
    }
}

inline void MatrixEffect::render(Canvas& c, int64_t f) {
  c.clear(0);
  constexpr uint8_t kHeadR = 175, kHeadG = 255, kHeadB = 175;
  constexpr uint8_t kTrailR = 27, kTrailG = 130, kTrailB = 39;
  // One falling stream per column: pos is that column's own clock, and pos/span re-rolls it on
  // every wrap so the speed, trail length and gaps change each time round.
  const int span = c.height() + 8;
  for (int x = 0; x < c.width(); ++x) {
    const uint32_t col = noise::hash2(static_cast<uint32_t>(x), 0x4D545258u);
    const int64_t pos =
        (f * static_cast<int64_t>(2u + col % 2u)) / 2 + static_cast<int>(col % static_cast<uint32_t>(span));
    const uint32_t roll = noise::hash2(col, static_cast<uint32_t>(pos / span));
    if (roll % 5u == 0) continue;
    const int head = static_cast<int>(pos % span);
    const int len = 4 + static_cast<int>(roll % 4u);
    for (int tr = 0; tr < len; ++tr) {
      const int y = head - tr;
      if (y < 0 || y >= c.height()) continue;
      if (tr == 0) {
        const uint8_t hs = (roll & 8u) ? 255 : 200;
        c.setPixel(x, y, color::pack(color::scale8(kHeadR, hs), color::scale8(kHeadG, hs),
                                     color::scale8(kHeadB, hs)));
        continue;
      }
      const uint8_t s = static_cast<uint8_t>(255u >> (tr - 1));
      const uint32_t px = color::pack(color::scale8(kTrailR, s), color::scale8(kTrailG, s),
                                      color::scale8(kTrailB, s));
      if (px != color::kBlack) c.setPixel(x, y, px);
    }
  }
}

inline void SwirlInEffect::render(Canvas& c, int64_t f) {
  c.clear(0);
  const int cx = c.width() / 2, cy = c.height() / 2;
  for (int i = 0; i < 48; ++i) {
    float a = i * 0.5f + f * kPhasePerStep;
    float r = (48 - i) / 6.0f;
    c.setPixel(cx + static_cast<int>(std::cos(a) * r), cy + static_cast<int>(std::sin(a) * r),
               paletteColor(static_cast<uint8_t>(i * 5), color::fromHsv((i * 9) % 360, 100, 70)));
  }
}

inline void SwirlOutEffect::render(Canvas& c, int64_t f) {
  c.clear(0);
  const int cx = c.width() / 2, cy = c.height() / 2;
  for (int i = 0; i < 48; ++i) {
    float a = i * 0.5f - f * kPhasePerStep;
    float r = i / 6.0f;
    c.setPixel(cx + static_cast<int>(std::cos(a) * r), cy + static_cast<int>(std::sin(a) * r),
               paletteColor(static_cast<uint8_t>(i * 5), color::fromHsv((i * 9) % 360, 100, 70)));
  }
}

inline void LookingEyesEffect::render(Canvas& c, int64_t f) {
  c.clear(0x000000u);
  const int px = static_cast<int>(std::sin(f * kPhasePerStep) * 1.5f);
  const int py = static_cast<int>(std::cos(f * 1.4f * kPhasePerStep));
  const int kEyeW = 4;
  const int cx = c.width() / 2;
  const int leftEye = std::max(0, cx - 2 * kEyeW);
  const int rightEye = std::min(c.width() - kEyeW, cx + kEyeW);
  for (int ex : {leftEye, rightEye}) {
    c.fillRect(ex, 2, 4, 4, 0xFFFFFFu);
    c.setPixel(ex + 1 + (px > 0 ? 2 : (px < 0 ? 0 : 1)), 3 + (py > 0 ? 1 : 0), 0x000000u);
    c.setPixel(ex + 2 + (px > 0 ? 1 : (px < 0 ? -1 : 0)), 3 + (py > 0 ? 1 : 0), 0x000000u);
  }
}

inline void TwinklingStarsEffect::render(Canvas& c, int64_t f) {
  c.clear(0);
  for (int i = 0; i < 22; ++i) {
    const uint32_t star = noise::hash2(static_cast<uint32_t>(i), 0x53544152u);
    int x = static_cast<int>(star % static_cast<uint32_t>(c.width()));
    int y = static_cast<int>((star >> 8) % static_cast<uint32_t>(c.height()));
    int ph = static_cast<int>((f + (star >> 16) % 30u) % 30);
    int b = ph < 15 ? ph * 16 : (30 - ph) * 16;
    const int bc = b > 255 ? 255 : b;
    if (b > 20) c.setPixel(x, y, paletteColor(static_cast<uint8_t>(bc), color::fromRgb(bc, bc, bc)));
  }
}

inline void ColorWavesEffect::render(Canvas& c, int64_t f) {
  for (int y = 0; y < c.height(); ++y)
    for (int x = 0; x < c.width(); ++x)
      c.setPixel(x, y, paletteColor(static_cast<uint8_t>(x * 10 + f),
                                    color::fromHsv(static_cast<int>(x * 10 + f) % 360, 100, 45)));
}

}
