#include "core/render/TransitionComposer.h"

#include <cmath>

namespace awtrix {
namespace render {

namespace {

uint32_t scale(uint32_t c, float f) {
  if (f < 0) f = 0;
  if (f > 1) f = 1;
  const uint32_t r = static_cast<uint32_t>(((c >> 16) & 0xFF) * f);
  const uint32_t g = static_cast<uint32_t>(((c >> 8) & 0xFF) * f);
  const uint32_t b = static_cast<uint32_t>((c & 0xFF) * f);
  return (r << 16) | (g << 8) | b;
}

uint32_t mix(uint32_t a, uint32_t b, float t) {
  auto ch = [&](int sh) {
    const int va = (a >> sh) & 0xFF, vb = (b >> sh) & 0xFF;
    return static_cast<uint32_t>(va + (vb - va) * t);
  };
  return (ch(16) << 16) | (ch(8) << 8) | ch(0);
}

// Cheap integer hash, only used to give each pixel or block a stable place in the reveal order.
uint32_t hash2(int x, int y) {
  uint32_t h = static_cast<uint32_t>(x) * 374761393u + static_cast<uint32_t>(y) * 668265263u;
  h = (h ^ (h >> 13)) * 1274126177u;
  return h ^ (h >> 16);
}

// Stable 0..1 value per column, used to stagger the falling lanes so they don't move in lockstep.
float lane01(int x) { return static_cast<float>(hash2(x, 0x5A17) >> 8) * (1.0f / 16777216.0f); }

float clamp01(float v) { return v < 0 ? 0 : (v > 1 ? 1 : v); }

float smoothstep(float p) { return p * p * (3.0f - 2.0f * p); }

constexpr float kLaneStagger = 0.5f;

void copyCanvas(Canvas& out, const Canvas& src) {
  for (int y = 0; y < out.height(); ++y)
    for (int x = 0; x < out.width(); ++x) out.setPixel(x, y, src.getPixel(x, y));
}

// One eighth of the panel, but never narrower than 4 columns unless the panel itself is smaller.
int blindWidth(int w) {
  const int bw = w / 8;
  return bw > 4 ? bw : (w < 4 ? (w > 0 ? w : 1) : 4);
}

}

void composeTransition(Canvas& out, const Canvas& from, const Canvas& to, Transition effect, float p,
                       int dir) {
  const int w = out.width(), h = out.height();

  if (p <= 0.0f) { copyCanvas(out, from); return; }
  if (p >= 1.0f) { copyCanvas(out, to); return; }

  if (transitionPacing(effect) == Pacing::Eased) p = smoothstep(p);

  const bool backward = dir < 0;

  switch (effect) {
    case Transition::Dim: {
      for (int y = 0; y < h; ++y)
        for (int x = 0; x < w; ++x)
          out.setPixel(x, y, p < 0.5f ? scale(from.getPixel(x, y), 1.0f - 2 * p)
                                      : scale(to.getPixel(x, y), 2 * p - 1.0f));
      return;
    }
    case Transition::Zoom: {
      const float cx = (w - 1) / 2.0f, cy = (h - 1) / 2.0f;
      for (int y = 0; y < h; ++y)
        for (int x = 0; x < w; ++x) {
          // The new frame is magnified by 1/p, which is meaningless at the very start, so hold.
          if (p <= 0.02f) { out.setPixel(x, y, from.getPixel(x, y)); continue; }
          const int sx = static_cast<int>(cx + (x - cx) / p);
          const int sy = static_cast<int>(cy + (y - cy) / p);
          const bool inside = sx >= 0 && sx < w && sy >= 0 && sy < h;
          out.setPixel(x, y, inside ? to.getPixel(sx, sy) : from.getPixel(x, y));
        }
      return;
    }
    case Transition::Rotate: {
      const int off = static_cast<int>(p * h) * (backward ? -1 : 1);
      for (int y = 0; y < h; ++y)
        for (int x = 0; x < w; ++x) {
          const int sy = y + off;
          uint32_t px;
          if (sy >= 0 && sy < h) px = from.getPixel(x, sy);
          else px = to.getPixel(x, ((sy % h) + h) % h);
          out.setPixel(x, y, px);
        }
      return;
    }
    case Transition::Pixelate: {
      const uint32_t thresh = static_cast<uint32_t>(p * 4294967295.0f);
      for (int y = 0; y < h; ++y)
        for (int x = 0; x < w; ++x)
          out.setPixel(x, y, hash2(x, y) < thresh ? to.getPixel(x, y) : from.getPixel(x, y));
      return;
    }
    case Transition::Curtain: {
      const int reveal = static_cast<int>(p * (w / 2 + 1));
      for (int y = 0; y < h; ++y)
        for (int x = 0; x < w; ++x) {
          const int edge = x < w / 2 ? x : (w - 1 - x);
          out.setPixel(x, y, edge < reveal ? to.getPixel(x, y) : from.getPixel(x, y));
        }
      return;
    }
    case Transition::Ripple: {
      const float cx = (w - 1) / 2.0f, cy = (h - 1) / 2.0f;
      const float maxR = std::sqrt(cx * cx + cy * cy);
      const float r = p * maxR;
      for (int y = 0; y < h; ++y)
        for (int x = 0; x < w; ++x) {
          const float dx = x - cx, dy = y - cy;
          out.setPixel(x, y, (dx * dx + dy * dy) <= r * r ? to.getPixel(x, y)
                                                          : from.getPixel(x, y));
        }
      return;
    }
    case Transition::Blink: {
      const bool second = p >= 0.5f;
      const float half = second ? (p - 0.5f) * 2 : p * 2;
      const int steps = 3;
      const int stepIdx = static_cast<int>(clamp01(half) * steps);
      const float lvl = second ? static_cast<float>(stepIdx) / steps
                               : 1.0f - static_cast<float>(stepIdx) / steps;
      for (int y = 0; y < h; ++y)
        for (int x = 0; x < w; ++x)
          out.setPixel(x, y, scale((second ? to : from).getPixel(x, y), lvl));
      return;
    }
    case Transition::Reload: {
      const int cut = static_cast<int>(p * w);
      for (int y = 0; y < h; ++y)
        for (int x = 0; x < w; ++x)
          out.setPixel(x, y, x < cut ? to.getPixel(x, y) : from.getPixel(x, y));
      return;
    }
    case Transition::Fade: {
      for (int y = 0; y < h; ++y)
        for (int x = 0; x < w; ++x)
          out.setPixel(x, y, mix(from.getPixel(x, y), to.getPixel(x, y), p));
      return;
    }

    case Transition::Cover: {
      const int off = static_cast<int>(p * w);
      for (int y = 0; y < h; ++y)
        for (int x = 0; x < w; ++x) {
          uint32_t px;
          if (backward) px = x < off ? to.getPixel(x + w - off, y) : from.getPixel(x, y);
          else px = x >= w - off ? to.getPixel(x - (w - off), y) : from.getPixel(x, y);
          out.setPixel(x, y, px);
        }
      return;
    }
    case Transition::Uncover: {
      const int off = static_cast<int>(p * w);
      for (int y = 0; y < h; ++y)
        for (int x = 0; x < w; ++x) {
          const int sx = backward ? x - off : x + off;
          out.setPixel(x, y, (sx >= 0 && sx < w) ? from.getPixel(sx, y) : to.getPixel(x, y));
        }
      return;
    }
    case Transition::Split: {
      const int c = w / 2;
      const int reveal = static_cast<int>(p * (c + 1));
      for (int y = 0; y < h; ++y)
        for (int x = 0; x < w; ++x) {
          const int edge = x < c ? (c - 1 - x) : (x - c);
          out.setPixel(x, y, edge < reveal ? to.getPixel(x, y) : from.getPixel(x, y));
        }
      return;
    }
    case Transition::Blinds: {
      const int bw = blindWidth(w);
      const int reveal = static_cast<int>(p * bw);
      for (int y = 0; y < h; ++y)
        for (int x = 0; x < w; ++x)
          out.setPixel(x, y, (x % bw) < reveal ? to.getPixel(x, y) : from.getPixel(x, y));
      return;
    }
    case Transition::Blocks: {
      const uint32_t thresh = static_cast<uint32_t>(p * 4294967295.0f);
      for (int y = 0; y < h; ++y)
        for (int x = 0; x < w; ++x)
          out.setPixel(x, y, hash2(x / 4, y / 2) < thresh ? to.getPixel(x, y)
                                                          : from.getPixel(x, y));
      return;
    }
    case Transition::Flash: {
      const uint32_t white = 0xFFFFFFu;
      for (int y = 0; y < h; ++y)
        for (int x = 0; x < w; ++x)
          out.setPixel(x, y, p < 0.5f ? mix(from.getPixel(x, y), white, p * 2)
                                      : mix(white, to.getPixel(x, y), p * 2 - 1.0f));
      return;
    }
    case Transition::Diamond: {
      const float cx = (w - 1) / 2.0f, cy = (h - 1) / 2.0f;
      const float d = p * (cx + cy);
      for (int y = 0; y < h; ++y)
        for (int x = 0; x < w; ++x)
          out.setPixel(x, y, (std::fabs(x - cx) + std::fabs(y - cy)) <= d ? to.getPixel(x, y)
                                                                          : from.getPixel(x, y));
      return;
    }
    // A sine-shaped vertical edge sweeps across the panel. span pads the travel by one amplitude
    // at each end so the crest is fully off screen at both p=0 and p=1.
    case Transition::Wave: {
      const float amp = h * 0.5f;
      const float span = w + 2 * amp;
      const float front = p * span - amp;
      const float k = 6.2831853f / (h > 1 ? h - 1 : 1);
      for (int y = 0; y < h; ++y) {
        const float edge = front + amp * std::sin(y * k);
        for (int x = 0; x < w; ++x) {
          const int col = backward ? (w - 1 - x) : x;
          out.setPixel(x, y, col < edge ? to.getPixel(x, y) : from.getPixel(x, y));
        }
      }
      return;
    }
    // Each column starts up to kLaneStagger late and then falls the full height in whatever time
    // is left, dragging the new frame down behind it.
    case Transition::Rain: {
      for (int x = 0; x < w; ++x) {
        const float delay = kLaneStagger * lane01(x);
        const int off =
            static_cast<int>(std::lround(clamp01((p - delay) / (1.0f - kLaneStagger)) * h));
        for (int y = 0; y < h; ++y) {
          const int sy = backward ? y + off : y - off;
          out.setPixel(x, y, (sy >= 0 && sy < h)
                                 ? from.getPixel(x, sy)
                                 : to.getPixel(x, backward ? sy - h : sy + h));
        }
      }
      return;
    }
    // Same staggered fall as Rain, except the new frame sits still and is uncovered rather than
    // dragged along.
    case Transition::Melt: {
      for (int x = 0; x < w; ++x) {
        const float delay = kLaneStagger * lane01(x);
        const int off =
            static_cast<int>(std::lround(clamp01((p - delay) / (1.0f - kLaneStagger)) * h));
        for (int y = 0; y < h; ++y) {
          const int sy = backward ? y + off : y - off;
          out.setPixel(x, y, (sy >= 0 && sy < h) ? from.getPixel(x, sy) : to.getPixel(x, y));
        }
      }
      return;
    }
    // Odd and even rows slide out to opposite sides, with the new frame wrapping in behind them.
    case Transition::Interlace: {
      const int off = static_cast<int>(p * w);
      for (int y = 0; y < h; ++y) {
        const bool left = ((y & 1) == 0) != backward;
        for (int x = 0; x < w; ++x) {
          const int sx = left ? x + off : x - off;
          out.setPixel(x, y, (sx >= 0 && sx < w) ? from.getPixel(sx, y)
                                                 : to.getPixel(left ? sx - w : sx + w, y));
        }
      }
      return;
    }

    // Slide, and the catch-all: Random and Count are resolved away before we get here.
    case Transition::Slide:
    case Transition::Random:
    case Transition::Count:
    default: {
      const int off = static_cast<int>(p * w);
      for (int y = 0; y < h; ++y)
        for (int x = 0; x < w; ++x) {
          const int sx = backward ? (x - off) : (x + off);
          uint32_t px;
          if (backward)
            px = sx >= 0 ? from.getPixel(sx, y) : to.getPixel(sx + w, y);
          else
            px = sx < w ? from.getPixel(sx, y) : to.getPixel(sx - w, y);
          out.setPixel(x, y, px);
        }
      return;
    }
  }
}

}
}
