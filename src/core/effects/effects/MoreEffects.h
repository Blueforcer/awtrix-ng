#pragma once

#include <cstdint>

#include <algorithm>
#include <cmath>

#include "core/effects/EffectMath.h"
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

AWTRIX_EFFECT(MovingLineEffect, "MovingLine", rate::kGentle);
AWTRIX_EFFECT(RadarEffect, "Radar", rate::kContinuous);
AWTRIX_EFFECT(CheckerboardEffect, "Checkerboard", rate::kContinuous);
AWTRIX_EFFECT(FireworksEffect, "Fireworks", rate::kSteady);
AWTRIX_EFFECT(PlasmaCloudEffect, "PlasmaCloud", rate::kContinuous);
AWTRIX_EFFECT(RippleEffect, "Ripple", rate::kBrisk);
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
  // A full-width line bouncing between top and bottom, the two rows it just left glowing behind.
  static const uint8_t kGlow[3] = {255, 70, 18};
  const uint8_t idx = static_cast<uint8_t>(f);
  const uint32_t col = paletteColorOr(idx, [idx] { return fx::hueColor(idx, 80); });
  for (int t = 2; t >= 0; --t)
    c.fillRect(0, fx::bounce(f - t, c.height()), c.width(), 1, fx::dim(col, kGlow[t]));
}

inline void RadarEffect::render(Canvas& c, int64_t f) {
  // One clockwise sweep every 256 steps; each pixel glows by how far the beam has already passed
  // it, so the afterglow is smooth at any panel size.
  constexpr uint32_t kGlowSpan = 65536 / 5;
  const uint16_t beam = static_cast<uint16_t>(f * 256);
  const float cx = c.width() * 0.5f, cy = c.height() * 0.5f;
  for (int y = 0; y < c.height(); ++y)
    for (int x = 0; x < c.width(); ++x) {
      const uint16_t a = fx::angle16(x + 0.5f - cx, y + 0.5f - cy);
      const uint16_t behind = static_cast<uint16_t>(beam - a);
      if (behind >= kGlowSpan) {
        c.setPixel(x, y, 0);
        continue;
      }
      const float q = 1.0f - static_cast<float>(behind) / kGlowSpan;
      const uint32_t col = paletteColorOr(static_cast<uint8_t>(a >> 8), [] { return 0x00E040u; });
      c.setPixel(x, y, fx::dim(col, fx::level(q * q)));
    }
}

inline void CheckerboardEffect::render(Canvas& c, int64_t f) {
  // Two opposite palette colours drifting at different speeds; squares grow with the panel height.
  const int cell = std::max(1, c.height() / 8);
  const uint8_t i1 = static_cast<uint8_t>(f * 3 / 5);
  const uint8_t i2 = static_cast<uint8_t>(f + 128);
  const uint32_t a = paletteColorOr(i1, [i1] { return fx::hueColor(i1, 70); });
  const uint32_t b = paletteColorOr(i2, [i2] { return fx::hueColor(i2, 70); });
  for (int y = 0; y < c.height(); ++y)
    for (int x = 0; x < c.width(); ++x) c.setPixel(x, y, ((x / cell + y / cell) & 1) ? b : a);
}

inline void FireworksEffect::render(Canvas& c, int64_t f) {
  c.clear(0);
  const int w = c.width(), h = c.height();
  // Independent launch lanes, more on bigger panels. Each lane waits, fires a rocket that slows
  // towards its peak, and bursts into sparks that spread, droop and flicker out.
  constexpr int kCycle = 64;
  constexpr int kBurst = 22;
  const int lanes = std::max(2, std::min(12, w * h / 64));
  const float reach = std::max(2.5f, std::min(w, h) * 0.4f);
  for (int lane = 0; lane < lanes; ++lane) {
    const uint32_t laneSeed = noise::hash2(static_cast<uint32_t>(lane), 0x46495245u);
    const int64_t t = f + laneSeed % kCycle;
    const uint32_t roll = noise::hash2(static_cast<uint32_t>(t / kCycle) * 131u + lane, 0x53484F54u);
    const int x = static_cast<int>(roll % static_cast<uint32_t>(w));
    const int peak = static_cast<int>((roll >> 8) % static_cast<uint32_t>(std::max(1, h / 2)));
    const int rise = std::max(4, (h - peak) * 2 / 3);
    const int age = static_cast<int>(t % kCycle) - static_cast<int>((roll >> 16) % 16u);
    if (age < 0) continue;
    if (age < rise) {
      const float p = 1.0f - static_cast<float>(age) / rise;
      const int y = fx::nearest(peak + (h - 1 - peak) * p * p);
      fx::lighten(c, x, y, 0xFFD8A0u);
      fx::lighten(c, x, y + 1, 0x402810u);
      continue;
    }
    const int b = age - rise;
    if (b >= kBurst) continue;
    const float k = static_cast<float>(b) / kBurst;
    const uint8_t idx = static_cast<uint8_t>(roll >> 24);
    const uint32_t col = paletteColorOr(idx, [idx] { return fx::hueColor(idx, 100); });
    if (b < 2) fx::lighten(c, x, peak, 0xFFFFFFu);
    const int sparks = 8 + static_cast<int>(roll % 5u);
    const float spread = 1.0f - (1.0f - k) * (1.0f - k) * (1.0f - k);
    const float fade = (1.0f - k) * (1.0f - k);
    for (int s = 0; s < sparks; ++s) {
      const uint32_t sr = noise::hash2(roll + static_cast<uint32_t>(s), 0x5350524Bu);
      if (k > 0.55f && (noise::hash2(sr, static_cast<uint32_t>(b)) & 1u)) continue;
      const float angle = (s + (sr & 0xFFu) / 512.0f) * 6.2831853f / sparks;
      const float r = reach * (0.65f + (sr >> 8 & 0xFFu) / 730.0f) * spread;
      const float px = x + std::cos(angle) * r;
      const float py = peak + std::sin(angle) * r + reach * 0.35f * k * k;
      fx::lighten(c, fx::nearest(px), fx::nearest(py), fx::dim(col, fx::level(fade)));
    }
  }
}

inline void PlasmaCloudEffect::render(Canvas& c, int64_t f) {
  // Two octaves of drifting value noise, stretched so the clouds span over half the palette, while
  // the whole range slowly shifts round it.
  const float z = f * 0.012f;
  const uint8_t shift = static_cast<uint8_t>(f / 5);
  for (int y = 0; y < c.height(); ++y) {
    noise::NoiseRow coarse(y / 10.0f, z, 0x434C4F55u);
    noise::NoiseRow fine(y / 5.0f, z * 1.7f, 0x44524946u);
    for (int x = 0; x < c.width(); ++x) {
      const float n = coarse.at(x / 10.0f) * 0.7f + fine.at(x / 5.0f) * 0.3f;
      const uint8_t idx = static_cast<uint8_t>(static_cast<int>((n - 0.5f) * 320.0f) + shift);
      c.setPixel(x, y, paletteColorOr(idx, [idx] { return fx::hueColor(idx, 55); }));
    }
  }
}

inline void RippleEffect::render(Canvas& c, int64_t f) {
  c.clear(0);
  const int w = c.width(), h = c.height();
  // Rings grow one pixel per step from a random point and leave a fading wake. Wide or tall panels
  // run several staggered ripples so the whole surface stays alive.
  constexpr float kWake = 6.0f;
  const int lanes = std::max(1, std::min(4, w * h / 256));
  const float reach = std::min(30.0f, std::sqrt(static_cast<float>(w * w + h * h)));
  const int period = static_cast<int>(reach + kWake) + 4;
  for (int lane = 0; lane < lanes; ++lane) {
    const int64_t t = f + static_cast<int64_t>(lane) * period / lanes;
    const uint32_t roll = noise::hash2(static_cast<uint32_t>(t / period) * 4u + lane, 0x5249504Cu);
    const float r = static_cast<float>(t % period);
    const float cx = roll % static_cast<uint32_t>(w) + 0.5f;
    const float cy = (roll >> 8) % static_cast<uint32_t>(h) + 0.5f;
    const float amp = 1.0f - r / period;
    const uint8_t idx = static_cast<uint8_t>(roll >> 16);
    const uint32_t col = paletteColorOr(idx, [idx] { return fx::hueColor(idx, 80); });
    const int x0 = std::max(0, static_cast<int>(cx - r - 1)), x1 = std::min(w - 1, static_cast<int>(cx + r + 1));
    const int y0 = std::max(0, static_cast<int>(cy - r - 1)), y1 = std::min(h - 1, static_cast<int>(cy + r + 1));
    for (int y = y0; y <= y1; ++y)
      for (int x = x0; x <= x1; ++x) {
        const float dx = x + 0.5f - cx, dy = y + 0.5f - cy;
        const float s = r - std::sqrt(dx * dx + dy * dy);
        if (s <= -1.0f || s >= kWake + 1.5f) continue;
        const float q = s < 0.0f ? 1.0f + s : s < 1.5f ? 1.0f : 1.0f - (s - 1.5f) / kWake;
        fx::lighten(c, x, y, fx::dim(col, fx::level(q * q * amp)));
      }
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
  // every wrap so the speed, trail length and gaps change each time round. Trails scale with the
  // panel height.
  const int trail = std::max(4, c.height() / 2);
  const int span = c.height() + 2 * trail;
  for (int x = 0; x < c.width(); ++x) {
    const uint32_t col = noise::hash2(static_cast<uint32_t>(x), 0x4D545258u);
    const int64_t pos =
        (f * static_cast<int64_t>(2u + col % 2u)) / 2 + static_cast<int>(col % static_cast<uint32_t>(span));
    const uint32_t roll = noise::hash2(col, static_cast<uint32_t>(pos / span));
    if (roll % 5u == 0) continue;
    const int head = static_cast<int>(pos % span);
    const int len = trail + static_cast<int>(roll % static_cast<uint32_t>(trail));
    for (int tr = 0; tr < len; ++tr) {
      const int y = head - tr;
      if (y < 0 || y >= c.height()) continue;
      if (tr == 0) {
        const uint8_t hs = (roll & 8u) ? 255 : 200;
        c.setPixel(x, y, color::pack(color::scaleChannel8(kHeadR, hs), color::scaleChannel8(kHeadG, hs),
                                     color::scaleChannel8(kHeadB, hs)));
        continue;
      }
      const float q = 1.0f - static_cast<float>(tr - 1) / len;
      c.setPixel(x, y, fx::dim(color::pack(kTrailR, kTrailG, kTrailB), fx::level(q * q)));
    }
  }
}

namespace fx {
// Spiral arms over the whole panel: the palette index climbs with the distance from the centre
// and once per turn around it. direction +1 draws the arms inwards, -1 outwards.
template <typename Paint>
void swirl(Canvas& c, int64_t f, int direction, Paint&& paint) {
  const float cx = c.width() * 0.5f, cy = c.height() * 0.5f;
  const int drift = static_cast<int>(f * 2 * direction);
  for (int y = 0; y < c.height(); ++y)
    for (int x = 0; x < c.width(); ++x) {
      const float dx = x + 0.5f - cx, dy = y + 0.5f - cy;
      const int d = static_cast<int>(std::sqrt(dx * dx + dy * dy) * 16.0f);
      c.setPixel(x, y, paint(static_cast<uint8_t>(d + (angle16(dx, dy) >> 8) + drift)));
    }
}
}

inline void SwirlInEffect::render(Canvas& c, int64_t f) {
  fx::swirl(c, f, 1, [this](uint8_t i) { return paletteColorOr(i, [i] { return fx::hueColor(i, 70); }); });
}

inline void SwirlOutEffect::render(Canvas& c, int64_t f) {
  fx::swirl(c, f, -1, [this](uint8_t i) { return paletteColorOr(i, [i] { return fx::hueColor(i, 70); }); });
}

inline void LookingEyesEffect::render(Canvas& c, int64_t f) {
  c.clear(0x000000u);
  // 8x8 ball per eye, drawn row by row instead of from a bitmap: x offset and width of each row.
  // Taller panels scale the whole face up by whole pixels, as far as the width allows.
  static const uint8_t kBallX[8] = {2, 1, 0, 0, 0, 0, 1, 2};
  static const uint8_t kBallW[8] = {4, 6, 8, 8, 8, 8, 6, 4};
  const int s = std::max(1, std::min(c.height() / 8, c.width() / 20));
  const int cx = c.width() / 2;
  const int top0 = (c.height() - 8 * s) / 2;
  const int eyeX[2] = {std::max(0, std::min(c.width() - 8 * s, cx - 10 * s)),
                       std::max(0, std::min(c.width() - 8 * s, cx + 2 * s))};

  // Gaze: a slot is 60 frames (~1.4 s), and every other slot keeps the target its predecessor
  // picked, so a look is held for 1.4 s or 2.9 s. The move itself is a 3-frame saccade.
  // Both axes are drawn from tables that crowd the middle: extreme stares stay rare.
  static const uint8_t kGazeX[16] = {2, 3, 2, 4, 3, 1, 2, 3, 4, 2, 3, 0, 3, 2, 5, 3};
  static const uint8_t kGazeY[8] = {2, 3, 2, 3, 1, 3, 2, 4};
  auto gaze = [](int64_t slot, int axis) {
    const uint32_t h = noise::hash2(static_cast<uint32_t>(slot), 0x45594553u);
    const uint32_t r = (h & 1u) ? h : noise::hash2(static_cast<uint32_t>(slot - 1), 0x45594553u);
    return axis ? kGazeY[(r >> 12) % 8u] : kGazeX[(r >> 4) % 16u];
  };
  const int64_t slot = f / 60;
  const int step = static_cast<int>(f % 60);
  const int mix = step < 3 ? step : 3;
  const int px = (gaze(slot - 1, 0) * (3 - mix) * s + gaze(slot, 0) * mix * s) / 3;
  const int py = (gaze(slot - 1, 1) * (3 - mix) * s + gaze(slot, 1) * mix * s) / 3;

  // Blink: one per 160-frame window (~3.8 s), at a phase the window's hash picks, so the rhythm
  // never settles. The lids snap shut and open again a little slower.
  int top = 0, bottom = 7;
  const int64_t window = f / 160;
  const int start = 10 + static_cast<int>(noise::hash2(static_cast<uint32_t>(window), 0x424C4E4Bu) % 140u);
  const int since = static_cast<int>(f % 160) - start;
  if (since >= 0 && since < 7) {
    static const uint8_t kLid[7] = {1, 3, 4, 4, 3, 2, 1};
    const int lid = kLid[since];
    top = lid;
    bottom = 7 - lid / 2;
  }

  for (const int x0 : eyeX) {
    for (int y = top; y <= bottom; ++y)
      c.fillRect(x0 + kBallX[y] * s, top0 + y * s, kBallW[y] * s, s, 0xFFFFFFu);
    c.fillRect(x0 + px, top0 + py, 2 * s, 2 * s, 0x000000u);
  }
}

inline void TwinklingStarsEffect::render(Canvas& c, int64_t f) {
  c.clear(0);
  const int w = c.width(), h = c.height();
  // Every star has its own cycle: it flares up quickly, fades slowly, rests, and comes back
  // somewhere else. The count follows the panel area.
  static const uint32_t kTints[4] = {0xFFFFFFu, 0xB8CCFFu, 0xFFE6C0u, 0xD8E4FFu};
  const int stars = std::max(6, std::min(96, w * h / 12));
  for (int i = 0; i < stars; ++i) {
    const uint32_t seed = noise::hash2(static_cast<uint32_t>(i), 0x53544152u);
    const int len = 40 + static_cast<int>(seed % 50u);
    const int64_t t = f + (seed >> 8) % static_cast<uint32_t>(len);
    const int phase = static_cast<int>(t % len);
    const int lit = len * 3 / 5;
    if (phase >= lit) continue;
    const uint32_t roll = noise::hash2(seed, static_cast<uint32_t>(t / len));
    const int rise = std::max(2, lit / 5);
    const float v = phase < rise ? static_cast<float>(phase + 1) / rise
                                 : 1.0f - static_cast<float>(phase - rise) / (lit - rise);
    const uint8_t idx = static_cast<uint8_t>(roll >> 16);
    const uint32_t col = paletteColorOr(idx, [roll] { return kTints[(roll >> 24) & 3u]; });
    fx::lighten(c, static_cast<int>(roll % static_cast<uint32_t>(w)),
                static_cast<int>((roll >> 8) % static_cast<uint32_t>(h)), fx::dim(col, fx::level(v * v)));
  }
}

inline void ColorWavesEffect::render(Canvas& c, int64_t f) {
  // The whole palette spread once across the width, sliding sideways.
  const int span = std::max(1, c.width() - 1);
  for (int x = 0; x < c.width(); ++x) {
    const uint8_t idx = static_cast<uint8_t>(x * 255 / span + f);
    c.fillRect(x, 0, 1, c.height(), paletteColorOr(idx, [idx] { return fx::hueColor(idx, 60); }));
  }
}

}
