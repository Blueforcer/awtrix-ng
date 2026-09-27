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
  const int w = c.width(), h = c.height();
  if (w < 3 || h < 4) return;
  const int columns = std::min(8, w / 2);
  const uint32_t fullWall = (1u << (2 * columns)) - 1u;
  uint32_t bricks = fullWall;
  int x = w / 2, y = h - 2, dx = 1, dy = -1;
  // Replay a bounded round, not elapsed uptime: seeks and dropped frames produce the same game.
  const int step = static_cast<int>((f % 512 + 512) % 512);
  for (int tick = 0; tick < step; ++tick) {
    int nx = x + dx, ny = y + dy;
    if (nx < 0 || nx >= w) { dx = -dx; nx = x + dx; }
    if (ny < 0 || ny > h - 2) { dy = -dy; ny = y + dy; }
    if (ny < 2) {
      const uint32_t hit = 1u << (ny * columns + nx * columns / w);
      if (bricks & hit) {
        bricks &= ~hit;
        dy = -dy;
        ny = y;  // Contact frame: bounce away from the brick, not through its neighbour.
      }
    }
    x = nx;
    y = ny;
    if (!bricks && y == h - 2) bricks = fullWall;
  }
  for (int row = 0; row < 2; ++row)
    for (int col = 0; col < columns; ++col) {
      if (!(bricks & (1u << (row * columns + col)))) continue;
      const int left = (col * w + columns - 1) / columns;
      const int right = ((col + 1) * w + columns - 1) / columns;
      c.fillRect(left, row, right - left - 1, 1, color::fromHsv(col * 40, 100, 55));
    }
  c.setPixel(x, y, 0xFFFFFFu);
  c.fillRect(std::max(0, std::min(w - 3, x - 1)), h - 1, 3, 1, 0x888888u);
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
  const int w = c.width(), h = c.height();
  if (w < 2 || h < 2) return;
  // Vertical serpentine: cross the whole height in seconds, then reverse at the far side.
  const int64_t span = static_cast<int64_t>(w) * h;
  const int64_t period = 2 * (span - 1);
  for (int i = 0; i < 6; ++i) {
    int64_t p = ((f % period) - i + period) % period;
    if (p >= span) p = period - p;
    const int x = static_cast<int>(p / h);
    const int row = static_cast<int>(p % h);
    const int y = x % 2 ? h - 1 - row : row;
    c.setPixel(x, y, paletteColor(static_cast<uint8_t>(i * 40), color::fromHsv(120, 100, 80 - i * 10)));
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

namespace fx {
template <typename Colour>
inline void travellingSwirl(Canvas& c, int64_t f, bool inward, Colour colour) {
  c.clear(0);
  if (c.width() < 2 || c.height() < 2) return;
  const float rx = (c.width() - 1) * 0.5f, ry = (c.height() - 1) * 0.5f;
  const int points = 2 * std::max(c.width(), c.height());
  const float phase = static_cast<float>((f % 200 + 200) % 200) / 200.0f;
  const float rotation = static_cast<float>((f % 720 + 720) % 720) * (6.2831853f / 720);
  // Two spiral packets move radially; modulo radius respawns particles at the opposite edge.
  for (int i = 0; i < points; ++i) {
    const float u = static_cast<float>(i) / (points - 1);
    const float r0 = 0.55f + u * 0.30f + (inward ? -phase : phase);
    const float r = r0 - std::floor(r0);
    for (int arm = 0; arm < 2; ++arm) {
      const float a = u * 6.2831853f + arm * 3.1415927f + (inward ? rotation : -rotation);
      const int x = static_cast<int>(std::lround(rx + rx * r * std::cos(a)));
      const int y = static_cast<int>(std::lround(ry + ry * r * std::sin(a)));
      c.setPixel(x, y, colour(static_cast<uint8_t>(u * 255)));
    }
  }
}
}

inline void SwirlInEffect::render(Canvas& c, int64_t f) {
  fx::travellingSwirl(c, f, true, [&](uint8_t index) {
    return paletteColor(index, color::fromHsv(index * 360 / 256, 100, 70));
  });
}

inline void SwirlOutEffect::render(Canvas& c, int64_t f) {
  fx::travellingSwirl(c, f, false, [&](uint8_t index) {
    return paletteColor(index, color::fromHsv(index * 360 / 256, 100, 70));
  });
}

inline void LookingEyesEffect::render(Canvas& c, int64_t f) {
  c.clear(0x000000u);
  // 8x8 ball per eye, drawn row by row instead of from a bitmap: x offset and width of each row.
  static const uint8_t kBallX[8] = {2, 1, 0, 0, 0, 0, 1, 2};
  static const uint8_t kBallW[8] = {4, 6, 8, 8, 8, 8, 6, 4};
  const int cx = c.width() / 2;
  const int eyeX[2] = {std::max(0, std::min(c.width() - 8, cx - 10)),
                       std::max(0, std::min(c.width() - 8, cx + 2))};

  // Gaze: a slot is 60 frames (~1.4 s), and every other slot keeps the target its predecessor
  // picked, so a look is held for 1.4 s or 2.9 s. The move itself is a 3-frame saccade.
  // Both axes are drawn from tables that crowd the middle: extreme stares stay rare.
  static const uint8_t kGazeX[16] = {2, 3, 2, 4, 3, 1, 2, 3, 4, 2, 3, 0, 3, 2, 5, 3};
  static const uint8_t kGazeY[8] = {2, 3, 2, 3, 1, 3, 2, 4};
  auto gaze = [](int64_t slot, int axis) {
    const uint32_t s = noise::hash2(static_cast<uint32_t>(slot), 0x45594553u);
    const uint32_t r = (s & 1u) ? s : noise::hash2(static_cast<uint32_t>(slot - 1), 0x45594553u);
    return axis ? kGazeY[(r >> 12) % 8u] : kGazeX[(r >> 4) % 16u];
  };
  const int64_t slot = f / 60;
  const int step = static_cast<int>(f % 60);
  const int mix = step < 3 ? step : 3;
  const int px = (gaze(slot - 1, 0) * (3 - mix) + gaze(slot, 0) * mix) / 3;
  const int py = (gaze(slot - 1, 1) * (3 - mix) + gaze(slot, 1) * mix) / 3;

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

  // The tables describe an eight-row sprite, not the panel geometry. Scale row
  // boundaries together so the ball, lids and pupil use the full canvas; at H=8
  // this is the original pixel-for-pixel rendering.
  const auto row = [&](int y) { return y * c.height() / 8; };
  for (const int x0 : eyeX) {
    for (int y = top; y <= bottom; ++y)
      c.fillRect(x0 + kBallX[y], row(y), kBallW[y], row(y + 1) - row(y), 0xFFFFFFu);
    c.fillRect(x0 + px, row(py), 2, row(py + 2) - row(py), 0x000000u);
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
