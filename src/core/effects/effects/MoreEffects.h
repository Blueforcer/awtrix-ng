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

// The three mini-games replay a bounded round from a fixed start instead of keeping state
// across frames: seeks, dropped frames and repeated calls all give the same picture. A round is
// kRound frames, so the worst case is kRound - 1 constant-cost ticks per frame. All state is
// fixed-size; nothing is allocated.
namespace fx {

constexpr int kGameRound = 512;

inline int clampInt(int v, int lo, int hi) { return v < lo ? lo : (v > hi ? hi : v); }

// Splits an effect frame into the round it belongs to and the tick within that round.
inline int roundTick(int64_t f, uint32_t& round) {
  const int tick = static_cast<int>((f % kGameRound + kGameRound) % kGameRound);
  round = static_cast<uint32_t>((f - tick) / kGameRound);
  return tick;
}

// Bricks are 3 LEDs wide with a 1-LED gap, 2 rows (3 from 11 panel rows up). The ball moves at
// 45 degrees or shallow (2 across per 1 down), picked by where it meets the 3-LED paddle. It
// moves one LED at a time and stops on a hit, so a tick removes at most one brick and the ball
// never stands on one.
struct BrickGame {
  static constexpr int kMaxColumns = 64;
  int w = 0, h = 0, rows = 0, columns = 0, left = 0, remaining = 0;
  uint64_t alive[3] = {};
  int x = 0, y = 0, sx = 1, sy = -1, aim = 0;
  bool shallow = false;
  uint32_t seed = 0, bounces = 0;

  void start(int width, int height, uint32_t round) {
    w = width;
    h = height;
    rows = h >= 11 ? 3 : 2;
    columns = std::min(kMaxColumns, (w + 1) / 4);
    left = (w - (4 * columns - 1)) / 2;
    refill();
    seed = noise::hash2(round, 0x42524B4Bu);
    bounces = 0;
    x = w / 2;
    y = h - 2;
    sx = (seed & 1u) ? 1 : -1;
    sy = -1;
    shallow = false;
    aim = 0;
  }
  void refill() {
    const uint64_t full = columns >= 64 ? ~0ull : (1ull << columns) - 1u;
    for (int r = 0; r < 3; ++r) alive[r] = r < rows ? full : 0;
    remaining = rows * columns;
  }
  // The brick index (row * kMaxColumns + column) of a live brick at (px, py), or -1.
  int brickAt(int px, int py) const {
    if (py < 0 || py >= rows || px < left) return -1;
    const int col = (px - left) / 4;
    if (col >= columns || (px - left) % 4 == 3 || !((alive[py] >> col) & 1u)) return -1;
    return py * kMaxColumns + col;
  }
  int paddleLeft() const { return clampInt(x + aim - 1, 0, w - 3); }
  // One LED step; true when it hit (and removed) a brick instead of moving.
  bool move(int mx, int my) {
    if (mx && (x + mx < 0 || x + mx >= w)) { sx = -sx; mx = -mx; }
    if (my && y + my < 0) { sy = -sy; my = -my; }
    const int b = brickAt(x + mx, y + my);
    if (b >= 0) {
      alive[b / kMaxColumns] &= ~(1ull << (b % kMaxColumns));
      --remaining;
      if (my) sy = -sy; else sx = -sx;
      return true;
    }
    x += mx;
    y += my;
    return false;
  }
  void tick() {
    if (sy > 0 && y >= h - 2) {
      // The paddle's aim decides the contact point: centre keeps 45 degrees, an edge sends the
      // ball out shallow towards that side. The next approach gets a new aim.
      const int rel = x - paddleLeft() - 1;
      shallow = rel != 0;
      if (rel) sx = rel;
      sy = -1;
      ++bounces;
      aim = static_cast<int>(noise::hash2(seed + bounces, 0x50414444u) % 3u) - 1;
    }
    if (!move(sx, sy) && shallow) move(sx, 0);
    if (!remaining && y >= h - 2) refill();
  }
};

// Paddles on columns 0 and w-1 always cover the ball's row; the receiving one steers towards an
// aim point, the other drifts back towards the ball's middle. The contact row picks the angle:
// the paddle edge sends it at 45 degrees, the middle shallow (1 down per 2 across).
struct PongGame {
  int w = 0, h = 0, paddle = 3;
  int x = 0, y = 0, sx = 1, sy = 1, aim = 0, top[2] = {0, 0};
  bool shallow = false;
  uint32_t seed = 0, rally = 0, ticks = 0;

  void start(int width, int height, uint32_t round) {
    w = width;
    h = height;
    paddle = h >= 11 ? 4 : 3;
    seed = noise::hash2(round, 0x504F4E47u);
    rally = ticks = 0;
    x = w / 2;
    y = h / 2;
    sx = (seed & 1u) ? 1 : -1;
    sy = (seed & 2u) ? 1 : -1;
    shallow = false;
    aim = static_cast<int>((seed >> 8) % static_cast<uint32_t>(paddle));
    top[0] = top[1] = clampInt(y - (paddle - 1) / 2, 0, h - paddle);
  }
  void tick() {
    ++ticks;
    if ((sx < 0 && x <= 1) || (sx > 0 && x >= w - 2)) {
      const int rel = y - top[sx < 0 ? 0 : 1];
      sx = -sx;
      // A corner ball meets the wall-side edge of a paddle pinned to the wall: that counts as
      // the middle, or 45-degree rallies could lock into corner-to-corner forever.
      shallow = (rel > 0 && rel < paddle - 1) || y == 0 || y == h - 1;
      if (rel == 0) sy = -1;
      if (rel == paddle - 1) sy = 1;
      ++rally;
      aim = static_cast<int>(noise::hash2(seed + rally, 0x41494D21u) % static_cast<uint32_t>(paddle));
    }
    x += sx;
    if (!shallow || (ticks & 1u)) {
      if (y + sy < 0 || y + sy >= h) sy = -sy;
      y += sy;
    }
    for (int side = 0; side < 2; ++side) {
      const bool receiving = (side == 0) == (sx < 0);
      int t = top[side];
      // The receiving paddle outpaces the ball (2 rows a tick against 1) so it reaches its aim
      // point; the other one lags at half a row a tick.
      if (receiving || (ticks & 1u)) {
        const int want = clampInt(y - (receiving ? aim : (paddle - 1) / 2), 0, h - paddle);
        const int reach = receiving ? 2 : 1;
        t += clampInt(want - t, -reach, reach);
      }
      // Keep covering the ball's row; it moves at most one row a tick.
      top[side] = clampInt(t, std::max(0, y - paddle + 1), std::min(h - paddle, y));
    }
  }
};

// Greedy snake on a field of up to 64x32 cells centred on the canvas. The head steers towards
// the food, avoiding its body and dead ends; food rows walk a per-round permutation of the rows
// so play covers the whole height. A stuck or full-length snake starts a new game at once.
struct SnakeGame {
  static constexpr int kMaxW = 64, kMaxH = 32, kMaxLen = 24, kStartLen = 4;
  int w = 0, h = 0, ox = 0, oy = 0, cap = kMaxLen;
  int len = 0, dir = 0, foodX = 0, foodY = 0, stride = 1, row0 = 0;
  uint8_t segX[kMaxLen] = {}, segY[kMaxLen] = {};  // [0] is the head
  uint64_t occupied[kMaxH] = {};
  uint32_t seed = 0, food = 0, games = 0;
  bool full = false;

  void start(int width, int height, uint32_t round) {
    w = std::min(width, kMaxW);
    h = std::min(height, kMaxH);
    ox = (width - w) / 2;
    oy = (height - h) / 2;
    cap = std::min(kMaxLen, w * h / 2);
    seed = noise::hash2(round, 0x534E4B45u);
    row0 = static_cast<int>((seed >> 8) % static_cast<uint32_t>(h));
    stride = h > 1 ? 1 + static_cast<int>(seed % static_cast<uint32_t>(h - 1)) : 1;
    while (gcd(stride, h) != 1) ++stride;
    food = games = 0;
    newGame();
  }
  static int gcd(int a, int b) { return b ? gcd(b, a % b) : a; }
  bool busy(int px, int py) const { return (occupied[py] >> px) & 1u; }
  void mark(int px, int py, bool on) {
    if (on) occupied[py] |= 1ull << px; else occupied[py] &= ~(1ull << px);
  }
  bool open(int px, int py) const { return px >= 0 && px < w && py >= 0 && py < h && !busy(px, py); }
  void newGame() {
    ++games;
    full = false;
    for (uint64_t& row : occupied) row = 0;
    len = std::min(kStartLen, cap);
    dir = 0;
    for (int i = 0; i < len; ++i) {
      segX[i] = static_cast<uint8_t>(len - 1 - i);
      segY[i] = static_cast<uint8_t>(h / 2);
      mark(segX[i], segY[i], true);
    }
    placeFood();
  }
  // At most cap occupied cells, so the raster probe ends within cap + 1 cells.
  void placeFood() {
    ++food;
    const int row = (row0 + static_cast<int>(food % static_cast<uint32_t>(h)) * stride) % h;
    int cell = row * w + static_cast<int>(noise::hash2(seed + food, 0x464F4F44u) % static_cast<uint32_t>(w));
    while (busy(cell % w, cell / w)) cell = (cell + 1) % (w * h);
    foodX = cell % w;
    foodY = cell / w;
  }
  void tick() {
    if (full) { newGame(); return; }
    static const int kDx[4] = {1, 0, -1, 0}, kDy[4] = {0, 1, 0, -1};
    const int tailX = segX[len - 1], tailY = segY[len - 1];
    mark(tailX, tailY, false);  // The tail moves on unless this step eats.
    int best = -1, bestScore = 0;
    for (int turn : {0, 1, 3}) {
      const int d = (dir + turn) % 4;
      const int nx = segX[0] + kDx[d], ny = segY[0] + kDy[d];
      if (!open(nx, ny)) continue;
      int exits = 0;
      for (int e = 0; e < 4; ++e)
        if (open(nx + kDx[e], ny + kDy[e])) ++exits;
      const bool eats = nx == foodX && ny == foodY;
      const int score = 4 * (std::abs(nx - foodX) + std::abs(ny - foodY)) + (exits || eats ? 0 : 1000) +
                        (turn ? 1 : 0);
      if (best < 0 || score < bestScore) { best = d; bestScore = score; }
    }
    if (best < 0) { newGame(); return; }
    const int nx = segX[0] + kDx[best], ny = segY[0] + kDy[best];
    const bool eats = nx == foodX && ny == foodY;
    if (eats) { mark(tailX, tailY, true); ++len; }
    for (int i = len - 1; i > 0; --i) { segX[i] = segX[i - 1]; segY[i] = segY[i - 1]; }
    segX[0] = static_cast<uint8_t>(nx);
    segY[0] = static_cast<uint8_t>(ny);
    mark(nx, ny, true);
    dir = best;
    if (eats) {
      full = len >= cap;
      placeFood();
    }
  }
};

template <typename Game>
inline void replayGame(Game& g, int w, int h, int64_t f) {
  uint32_t round = 0;
  const int ticks = roundTick(f, round);
  g.start(w, h, round);
  for (int t = 0; t < ticks; ++t) g.tick();
}

constexpr uint32_t kBallColour = 0xFFFFFFu, kPaddleColour = 0x888888u;
constexpr uint32_t kPongBall = 0x00FF88u, kPongPaddle = 0xAAAAAAu, kSnakeFood = 0xFF2000u;
constexpr int kBrickHue[3] = {0, 40, 200};

}

inline void BrickBreakerEffect::render(Canvas& c, int64_t f) {
  c.clear(0);
  const int w = c.width(), h = c.height();
  if (w < 3 || h < 4) return;
  fx::BrickGame g;
  fx::replayGame(g, w, h, f);
  for (int row = 0; row < g.rows; ++row)
    for (int col = 0; col < g.columns; ++col)
      if ((g.alive[row] >> col) & 1u)
        c.fillRect(g.left + 4 * col, row, 3, 1, color::fromHsv(fx::kBrickHue[row], 100, 55));
  c.setPixel(g.x, g.y, fx::kBallColour);
  c.fillRect(g.paddleLeft(), h - 1, 3, 1, fx::kPaddleColour);
}

inline void PingPongEffect::render(Canvas& c, int64_t f) {
  c.clear(0);
  const int w = c.width(), h = c.height();
  if (w < 4 || h < 3) return;
  fx::PongGame g;
  fx::replayGame(g, w, h, f);
  c.fillRect(0, g.top[0], 1, g.paddle, fx::kPongPaddle);
  c.fillRect(w - 1, g.top[1], 1, g.paddle, fx::kPongPaddle);
  c.setPixel(g.x, g.y, fx::kPongBall);
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
  if (w < 4 || h < 2) return;
  fx::SnakeGame g;
  fx::replayGame(g, w, h, f);
  c.setPixel(g.ox + g.foodX, g.oy + g.foodY, fx::kSnakeFood);
  // Head-to-tail gradient: palette index 0..255 when a palette is set, bright to dim green otherwise.
  const int last = std::max(1, g.len - 1);
  for (int i = 0; i < g.len; ++i)
    c.setPixel(g.ox + g.segX[i], g.oy + g.segY[i],
               paletteColor(static_cast<uint8_t>(i * 255 / last), color::fromHsv(120, 100, 90 - i * 60 / last)));
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
