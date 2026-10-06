#pragma once

#include <algorithm>
#include <cstdint>
#include <cstdlib>
#include <string>

#include "core/effects/EffectMath.h"
#include "core/effects/IEffect.h"
#include "core/effects/SimulationSlots.h"
#include "core/render/Color.h"

namespace awtrix {

namespace fx {

// Breakout on autopilot: the paddle follows the ball and aims with a different part of its
// face on every return, so the ball fans out over the wall instead of repeating one path.
struct Breakout {
  static constexpr int kMaxRows = 8;
  static constexpr int kMaxCols = 64;
  static constexpr int kServeSteps = 12;
  static constexpr int kClearedSteps = 30;
  static constexpr int kPopSteps = 3;

  uint64_t bricks[kMaxRows];
  Rng rng;
  int16_t w, h, left, bx, by, paddleX, popCol, popRow;
  int32_t idle;
  int8_t dx, dy, aim;
  uint8_t rows, cols, brickW, top, paddleW, pause, popAge;

  void reset(int width, int height, uint32_t seed) {
    w = static_cast<int16_t>(width);
    h = static_cast<int16_t>(height);
    rng.state = seed;
    rows = static_cast<uint8_t>(std::max(1, std::min({kMaxRows, h * 3 / 8, h - 4})));
    brickW = static_cast<uint8_t>(std::max(2, w / 16));
    cols = static_cast<uint8_t>(std::max(1, std::min(kMaxCols, w / brickW)));
    left = static_cast<int16_t>((w - cols * brickW) / 2);
    top = h >= 16 ? 1 : 0;
    paddleW = static_cast<uint8_t>(std::min<int>(w, std::max(3, w / 10)));
    refill();
    serve();
  }

  void refill() {
    const uint64_t row = cols >= 64 ? ~0ull : (1ull << cols) - 1;
    for (int r = 0; r < kMaxRows; ++r) bricks[r] = r < rows ? row : 0;
    idle = 0;
  }

  void serve() {
    paddleX = static_cast<int16_t>((w - paddleW) / 2);
    bx = static_cast<int16_t>(paddleX + paddleW / 2);
    by = static_cast<int16_t>(h - 2);
    dx = rng.chance(2) ? 1 : -1;
    dy = -1;
    aim = 0;
    pause = kServeSteps;
  }

  bool brick(int x, int y, int& col, int& row) const {
    row = y - top;
    if (row < 0 || row >= rows || x < left) return false;
    col = (x - left) / brickW;
    return col < cols && (bricks[row] >> col & 1u);
  }

  bool smash(int x, int y) {
    int col, row;
    if (!brick(x, y, col, row)) return false;
    bricks[row] &= ~(1ull << col);
    popCol = static_cast<int16_t>(col);
    popRow = static_cast<int16_t>(row);
    popAge = kPopSteps;
    idle = 0;
    return true;
  }

  bool cleared() const {
    for (int r = 0; r < rows; ++r)
      if (bricks[r]) return false;
    return true;
  }

  void step() {
    if (popAge) --popAge;
    if (pause) {
      if (--pause == 0 && cleared()) {
        refill();
        serve();
      }
      return;
    }

    const int target = std::max(0, std::min(w - paddleW, bx - paddleW / 2 - aim));
    paddleX = static_cast<int16_t>(paddleX + (target > paddleX) - (target < paddleX));

    int nx = bx + dx, ny = by + dy;
    if (nx < 0 || nx >= w) {
      dx = static_cast<int8_t>(-dx);
      nx = bx + dx;
    }
    if (ny < 0) {
      dy = static_cast<int8_t>(-dy);
      ny = by + dy;
    }

    // Faces first, then the corner: a ball that only grazes a brick diagonally rebounds straight
    // back, one that meets a face bounces off that face.
    bool vertical = smash(bx, ny);
    bool horizontal = smash(nx, by);
    if (!vertical && !horizontal && smash(nx, ny)) vertical = horizontal = true;
    if (vertical || horizontal) {
      if (vertical) dy = static_cast<int8_t>(-dy);
      if (horizontal) dx = static_cast<int8_t>(-dx);
      if (cleared()) pause = kClearedSteps;
      return;
    }

    if (ny >= h - 1) {
      const int rel = nx - paddleX;
      if (rel < -1 || rel > paddleW) {
        serve();
        return;
      }
      const int third = std::max(1, paddleW / 3);
      if (rel < third) dx = -1;
      else if (rel >= paddleW - third) dx = 1;
      dy = -1;
      aim = static_cast<int8_t>(rng.below(paddleW) - paddleW / 2);
      return;
    }

    bx = static_cast<int16_t>(nx);
    by = static_cast<int16_t>(ny);
    // Refills the wall after 24 * (w + h) moves without a brick.
    if (++idle > 24 * (w + h)) refill();
  }
};

// Pong on autopilot. The paddle the ball is heading for tracks it; now and then it misjudges
// the ball, which leaves the panel and is served again from the middle.
struct Pong {
  static constexpr int kServeSteps = 10;
  static constexpr int kPointSteps = 12;

  Rng rng;
  int16_t w, h, bx, by, paddle[2], wait;
  int8_t dx, dy, blunder;
  uint8_t paddleH;
  bool out;

  void reset(int width, int height, uint32_t seed) {
    w = static_cast<int16_t>(width);
    h = static_cast<int16_t>(height);
    rng.state = seed;
    paddleH = static_cast<uint8_t>(std::min<int>(h, std::max(2, h * 3 / 8)));
    paddle[0] = paddle[1] = static_cast<int16_t>((h - paddleH) / 2);
    serve(rng.chance(2) ? 1 : -1);
  }

  void serve(int direction) {
    bx = static_cast<int16_t>(w / 2);
    by = static_cast<int16_t>(rng.below(h));
    dx = static_cast<int8_t>(direction);
    dy = rng.chance(2) ? 1 : -1;
    wait = kServeSteps;
    out = false;
    pickBlunder();
  }

  void pickBlunder() { blunder = static_cast<int8_t>(rng.chance(7) ? (rng.chance(2) ? 1 : -1) : 0); }

  void movePaddle(int side, int target) {
    target = std::max(0, std::min(h - paddleH, target));
    paddle[side] = static_cast<int16_t>(paddle[side] + (target > paddle[side]) - (target < paddle[side]));
  }

  void step() {
    if (wait) {
      if (--wait == 0 && out) serve(dx > 0 ? -1 : 1);
      return;
    }
    const int defender = dx < 0 ? 0 : 1;
    movePaddle(defender, by - paddleH / 2 + blunder * (paddleH + 1));
    if ((bx & 1) == 0) movePaddle(1 - defender, (h - paddleH) / 2);

    int ny = by + dy;
    if (ny < 0 || ny >= h) {
      dy = static_cast<int8_t>(-dy);
      ny = by + dy;
    }
    const int nx = bx + dx;
    if (nx < 0 || nx >= w) {
      out = true;
      wait = kPointSteps;
      bx = static_cast<int16_t>(nx);
      return;
    }
    if (nx == 0 || nx == w - 1) {
      const int rel = ny - paddle[defender];
      if (rel >= 0 && rel < paddleH) {
        dx = static_cast<int8_t>(-dx);
        if (paddleH > 2 && rel < paddleH / 3) dy = -1;
        else if (paddleH > 2 && rel >= paddleH - paddleH / 3) dy = 1;
        pickBlunder();
        by = static_cast<int16_t>(ny);
        return;
      }
    }
    bx = static_cast<int16_t>(nx);
    by = static_cast<int16_t>(ny);
  }
};

// Snake on autopilot: it steers itself to the apple across the wrapping edges and avoids its body
// and cells with no way out. Trapped anyway, or grown to full length, it blinks and restarts.
struct SnakeRun {
  static constexpr int kMaxLength = 64;
  static constexpr int kEndSteps = 24;

  Rng rng;
  uint8_t xs[kMaxLength], ys[kMaxLength];
  int16_t w, h, appleX, appleY;
  uint8_t head, len, cap, dir, ending, hue;

  void reset(int width, int height, uint32_t seed) {
    w = static_cast<int16_t>(width);
    h = static_cast<int16_t>(height);
    rng.state = seed;
    len = static_cast<uint8_t>(std::min(3, w * h));
    cap = static_cast<uint8_t>(std::max<int>(len, std::min(kMaxLength, w * h / 3)));
    dir = 0;
    head = static_cast<uint8_t>(len - 1);
    for (int i = 0; i < len; ++i) {
      xs[i] = static_cast<uint8_t>(((w / 2 - (len - 1) + i) % w + w) % w);
      ys[i] = static_cast<uint8_t>(h / 2);
    }
    ending = 0;
    hue = static_cast<uint8_t>(rng.next());
    placeApple();
  }

  int segX(int i) const { return xs[(head - i + kMaxLength) % kMaxLength]; }
  int segY(int i) const { return ys[(head - i + kMaxLength) % kMaxLength]; }

  bool occupied(int x, int y, int segments) const {
    for (int i = 0; i < segments; ++i)
      if (segX(i) == x && segY(i) == y) return true;
    return false;
  }

  void neighbour(int d, int x, int y, int& nx, int& ny) const {
    static const int8_t kDx[4] = {1, 0, -1, 0};
    static const int8_t kDy[4] = {0, 1, 0, -1};
    nx = (x + kDx[d] + w) % w;
    ny = (y + kDy[d] + h) % h;
  }

  void placeApple() {
    for (int tries = 0; tries < 32; ++tries) {
      const int x = rng.below(w), y = rng.below(h);
      if (!occupied(x, y, len)) {
        appleX = static_cast<int16_t>(x);
        appleY = static_cast<int16_t>(y);
        return;
      }
    }
    for (int y = 0; y < h; ++y)
      for (int x = 0; x < w; ++x)
        if (!occupied(x, y, len)) {
          appleX = static_cast<int16_t>(x);
          appleY = static_cast<int16_t>(y);
          return;
        }
  }

  int wrapDistance(int a, int b, int n) const {
    const int d = std::abs(a - b);
    return std::min(d, n - d);
  }

  void step() {
    if (ending) {
      if (--ending == 0) reset(w, h, rng.next() | 1u);
      return;
    }
    int best = -1, bestScore = 0, bestX = 0, bestY = 0;
    for (int d = 0; d < 4; ++d) {
      if (len > 1 && d == (dir + 2) % 4) continue;
      int nx, ny;
      neighbour(d, segX(0), segY(0), nx, ny);
      const bool eats = nx == appleX && ny == appleY;
      // The tail moves out of the way this step unless the snake is growing.
      const int body = eats ? len : len - 1;
      if (occupied(nx, ny, body)) continue;
      int exits = 0;
      for (int e = 0; e < 4; ++e) {
        int ex, ey;
        neighbour(e, nx, ny, ex, ey);
        if (!occupied(ex, ey, body)) ++exits;
      }
      const int score = (wrapDistance(nx, appleX, w) + wrapDistance(ny, appleY, h)) * 4 +
                        (d != dir) + (exits == 0 ? 1000 : 0);
      if (best < 0 || score < bestScore) {
        best = d;
        bestScore = score;
        bestX = nx;
        bestY = ny;
      }
    }
    if (best < 0) {
      ending = kEndSteps;
      return;
    }
    dir = static_cast<uint8_t>(best);
    head = static_cast<uint8_t>((head + 1) % kMaxLength);
    xs[head] = static_cast<uint8_t>(bestX);
    ys[head] = static_cast<uint8_t>(bestY);
    if (bestX == appleX && bestY == appleY) {
      if (len < cap) ++len;
      hue = static_cast<uint8_t>(hue + 24);
      if (len >= cap) ending = kEndSteps;
      else placeApple();
    }
  }
};

}

class BrickBreakerEffect : public IEffect {
 public:
  const std::string& id() const override { return id_; }
  float rate() const override { return rate::kGentle; }
  void render(Canvas& c, int64_t frame) override {
    c.clear(0);
    const fx::PixelGrid px(c);
    const fx::Breakout* g = sims_.advance(px.width, px.height, speed(), frame);
    if (!g) return;
    for (int r = 0; r < g->rows; ++r) {
      const uint32_t col = paletteColorOr(static_cast<uint8_t>(r * 256 / g->rows),
                                          [r] { return fx::hueColor(static_cast<uint8_t>(r * 28), 80); });
      for (int k = 0; k < g->cols; ++k)
        if (g->bricks[r] >> k & 1u) px.fill(c, g->left + k * g->brickW, g->top + r, g->brickW - 1, 1, col);
    }
    if (g->popAge)
      px.fill(c, g->left + g->popCol * g->brickW, g->top + g->popRow, g->brickW - 1, 1,
              fx::dim(0xFFFFFFu, static_cast<uint8_t>(g->popAge * 255 / fx::Breakout::kPopSteps)));
    px.fill(c, g->paddleX, g->h - 1, g->paddleW, 1, 0x909090u);
    if (!g->cleared()) px.fill(c, g->bx, g->by, 1, 1, 0xFFFFFFu);
  }

 private:
  fx::SimulationSlots<fx::Breakout> sims_;
  std::string id_ = "BrickBreaker";
};

class PingPongEffect : public IEffect {
 public:
  const std::string& id() const override { return id_; }
  float rate() const override { return rate::kSteady; }
  bool usesPalette() const override { return false; }
  void render(Canvas& c, int64_t frame) override {
    c.clear(0);
    const fx::PixelGrid px(c);
    const fx::Pong* g = sims_.advance(px.width, px.height, speed(), frame);
    if (!g) return;
    if (g->w >= 16)
      for (int y = 0; y < g->h; y += 2) px.fill(c, g->w / 2, y, 1, 1, 0x202020u);
    px.fill(c, 0, g->paddle[0], 1, g->paddleH, 0xFFFFFFu);
    px.fill(c, g->w - 1, g->paddle[1], 1, g->paddleH, 0xFFFFFFu);
    if (!g->out) px.fill(c, g->bx, g->by, 1, 1, 0xFF0000u);
  }

 private:
  fx::SimulationSlots<fx::Pong> sims_;
  std::string id_ = "PingPong";
};

class SnakeEffect : public IEffect {
 public:
  const std::string& id() const override { return id_; }
  float rate() const override { return rate::kGentle; }
  void render(Canvas& c, int64_t frame) override {
    c.clear(0);
    const fx::PixelGrid px(c);
    const fx::SnakeRun* g = sims_.advance(px.width, px.height, speed(), frame);
    if (!g) return;
    if (!g->ending) px.fill(c, g->appleX, g->appleY, 1, 1, 0xFF1000u);
    if (g->ending && (g->ending / 3) % 2) return;
    for (int i = g->len - 1; i >= 0; --i) {
      const uint8_t shade = static_cast<uint8_t>(std::max(90, 255 - i * 8));
      const uint32_t col = paletteColorOr(static_cast<uint8_t>(g->hue + i * 6),
                                          [i] { return fx::hueColor(static_cast<uint8_t>(85 + i * 2), 90); });
      px.fill(c, g->segX(i), g->segY(i), 1, 1, fx::dim(col, shade));
    }
  }

 private:
  fx::SimulationSlots<fx::SnakeRun> sims_;
  std::string id_ = "Snake";
};

}
