#include "platform/tc002/voice/VoiceOverlay.h"
#include "platform/tc002/contract/tc002_layout.h"

#include <algorithm>
#include <cmath>
#include <cstdlib>

#include "core/effects/EffectMath.h"
#include "platform/tc002/runtime/Tc002Paint.h"

namespace awtrix::tc002::voice {
namespace {
constexpr uint32_t kBlue = 0x18bcf2, kWhite = 0xf4f8fc, kEye = 0xffffff,
                   kPink = 0xff5f96, kOrange = 0xff6a3d, kCloud = 0x6f8894,
                   kGrey = 0x8f9ba3, kDrop = 0x9fe8ff;
constexpr float kTau = 6.2831853f;
enum class Eyes { Open, Tall, Closed, Happy, Cross };
enum class Mouth { Smile, Round, Flat, Half, Wide, Frown };
struct Point {
  int x, y;
};
constexpr Point kBranches[] = {{9, 11}, {8, 12}, {5, 14}, {6, 15}};

using color::scale;
using fx::lighten;
using motion::easeOut;
using paint::nearest;

uint32_t mix(uint32_t a, uint32_t b, float t) {
  return color::mix<color::Rounding::Nearest>(a, b, t);
}

// The house of the Home Assistant logo: 15 x 16, roof at 45 degrees.
void house(Canvas& c, int ox, int oy, uint32_t color) {
  for (int y = 0; y < 16; ++y)
    for (int x = 0; x < 15; ++x)
      if (y >= 7 || std::abs(x - 7) <= y) c.setPixel(ox + x, oy + y, color);
}
// A logo node is a 3x3 disc; its corner towards the branch stays solid.
void node(Canvas& c, int cx, int cy, uint32_t color, Point joint) {
  for (int dy = -1; dy <= 1; ++dy)
    for (int dx = -1; dx <= 1; ++dx) {
      const bool soft = dx && dy && !(dx == joint.x && dy == joint.y);
      c.setPixel(cx + dx, cy + dy, soft ? mix(kBlue, color, .45f) : color);
    }
}
constexpr Point kHappy[] = {{-1, 10}, {0, 9}, {1, 10}};
constexpr Point kCross[] = {{-1, 8}, {1, 8}, {0, 9}, {-1, 10}, {1, 10}};
constexpr Point kSmile[] = {{5, 12}, {6, 13}, {7, 13}, {8, 13}, {9, 12}};
constexpr Point kFrown[] = {{5, 14}, {6, 13}, {7, 13}, {8, 13}, {9, 14}};
constexpr Point kRound[] = {{7, 12}, {7, 13}};
constexpr Point kFlat[] = {{6, 13}, {7, 13}, {8, 13}};
template <std::size_t N>
void plot(Canvas& c, int ox, int oy, const Point (&shape)[N], uint32_t color) {
  for (Point p : shape) c.setPixel(ox + p.x, oy + p.y, color);
}
void eyes(Canvas& c, int ox, int oy, Eyes kind, int dx = 0, int dy = 0) {
  ox += dx;
  oy += dy;
  if (kind == Eyes::Happy || kind == Eyes::Cross) {
    for (int centre : {4, 10}) {
      if (kind == Eyes::Happy)
        plot(c, ox + centre, oy, kHappy, kEye);
      else
        plot(c, ox + centre, oy, kCross, kEye);
    }
    return;
  }
  const int top = kind == Eyes::Tall ? 7 : kind == Eyes::Open ? 8 : 10;
  const int rows = kind == Eyes::Tall ? 4 : kind == Eyes::Open ? 3 : 1;
  c.fillRect(ox + 4, oy + top, 2, rows, kEye);
  c.fillRect(ox + 9, oy + top, 2, rows, kEye);
}
// The mouth is unlit LEDs inside the blue.
void mouth(Canvas& c, int ox, int oy, Mouth kind) {
  switch (kind) {
    case Mouth::Smile: return plot(c, ox, oy, kSmile, 0);
    case Mouth::Frown: return plot(c, ox, oy, kFrown, 0);
    case Mouth::Round: return plot(c, ox, oy, kRound, 0);
    case Mouth::Flat: return plot(c, ox, oy, kFlat, 0);
    case Mouth::Half:
    case Mouth::Wide:
      c.fillRect(ox + 6, oy + 12, 3, kind == Mouth::Wide ? 3 : 2, 0);
      if (kind == Mouth::Wide) c.setPixel(ox + 7, oy + 14, kPink);
  }
}
// Rises from below with a one-pixel overshoot during the first 400 ms.
int rise(float u) {
  if (u < .28f) {
    const float e = easeOut(u / .28f);
    return nearest(16.f * (1.f - e) - e);
  }
  return u < .4f ? nearest(-1.f + (u - .28f) / .12f) : 0;
}
// The logo wakes up: its side nodes slide into eyes while the tree fades.
void wake(Canvas& c, int oy, float u, bool listening) {
  const float morph = std::clamp((u - .36f) / .3f, 0.f, 1.f);
  const float e = easeOut(morph);
  house(c, 0, oy, kBlue);
  const uint32_t tree = mix(kWhite, kBlue, e);
  for (int y = 7; y <= 15; ++y) c.setPixel(7, oy + y, tree);
  for (Point b : kBranches) c.setPixel(b.x, oy + b.y, tree);
  node(c, 7, oy + 5, tree, {0, 0});
  if (morph < .55f) {
    node(c, nearest(3 + 1.5f * e), oy + nearest(12 - 3 * e), kWhite, {1, 1});
    node(c, nearest(11 - 1.5f * e), oy + 9, kWhite, {-1, 1});
    return;
  }
  eyes(c, 0, oy, u > .6f && u < .68f ? Eyes::Closed : Eyes::Open);
  mouth(c, 0, oy, listening ? Mouth::Round : Mouth::Smile);
}
void dots(Canvas& c, float u) {
  const int shown = static_cast<int>(11 * std::clamp(u / .45f, 0.f, 1.f));
  for (int i = 0; i < shown; ++i)
    c.fillRect(19 + 3 * i, 7, 2, 2, scale(kBlue, .35f));
}
// Newest level on the right; older ones move towards the house.
void bars(Canvas& c, const std::array<float, 11>& levels, float gain) {
  for (std::size_t i = 0; i < levels.size(); ++i) {
    const float level = levels[i] * gain, half = .5f + level * 7.f;
    const uint32_t color =
        mix(kBlue, kWhite, std::max(0.f, level - .45f) * 1.6f);
    const int x = 19 + 3 * static_cast<int>(i);
    for (int y = 0; y < 16; ++y) {
      const float cover =
          std::clamp(half + .5f - std::abs(y + .5f - 8.f), 0.f, 1.f);
      if (cover <= 0) continue;
      lighten(c, x, y, scale(color, .25f + .75f * cover));
      lighten(c, x + 1, y, scale(color, .25f + .75f * cover));
    }
  }
}
float talk(float t) {
  const float syllable = std::max(0.f, std::sin(t * kTau * 3.7f));
  const float word = .5f + .5f * std::sin(t * kTau * .63f + 1.2f);
  return syllable * (word > .28f ? 1.f : .12f) * (.55f + .45f * word);
}
// One arc leaves the mouth every 260 ms, blue near the house, grey far out.
void arcs(Canvas& c, float t) {
  constexpr float period = .26f, speed = 22.f, cx = 8.f, cy = 12.f,
                  squash = 1.15f;
  constexpr int left = 16;
  const int newest = static_cast<int>(t / period);
  for (int n = newest; n >= 0 && n > newest - 9; --n) {
    const float emitted = n * period, r = 6.f + (t - emitted) * speed;
    if (r > 50.f) continue;
    const float syllable = std::clamp(talk(emitted) * 1.3f, 0.f, 1.f);
    const uint32_t color =
        mix(mix(kBlue, kWhite, syllable * .8f), kGrey, (r - 12.f) / 34.f);
    for (int y = 0; y < c.height(); ++y) {
      const float dy = (y - cy) * squash;
      if (std::abs(dy) > r) continue;
      const int x = nearest(cx + std::sqrt(r * r - dy * dy));
      if (x >= left) lighten(c, x, y, color);
    }
    for (int x = left; x < c.width(); ++x) {
      const float dx = x - cx;
      if (std::abs(dx) > r) continue;
      const float h = std::sqrt(r * r - dx * dx) / squash;
      lighten(c, x, nearest(cy - h), color);
      lighten(c, x, nearest(cy + h), color);
    }
  }
}
void cloud(Canvas& c, int x, int y, int w, int h) {
  for (int i = 2; i < w - 2; ++i) {
    c.setPixel(x + i, y, kCloud);
    c.setPixel(x + i, y + h - 1, kCloud);
  }
  for (int j = 2; j < h - 2; ++j) {
    c.setPixel(x, y + j, kCloud);
    c.setPixel(x + w - 1, y + j, kCloud);
  }
  for (Point p : {Point{1, 1}, Point{w - 2, 1}, Point{1, h - 2},
                  Point{w - 2, h - 2}})
    c.setPixel(x + p.x, y + p.y, kCloud);
}
void think(Canvas& c, float since) {
  if (since > .05f) c.setPixel(16, 11, scale(kWhite, .55f));
  if (since > .15f) c.fillRect(18, 9, 2, 2, scale(kWhite, .55f));
  if (since <= .25f) return;
  const float grow = easeOut((since - .25f) / .25f);
  cloud(c, 21, 0, std::max(6, nearest(31 * grow)), 13);
  if (grow < 1) return;
  for (int i = 0; i < 3; ++i) {
    const float lift = std::max(0.f, std::sin(since * kTau * 1.5f - i * .9f));
    c.fillRect(28 + 7 * i, 5 - nearest(lift * 2), 3, 3,
               lift > .55f ? kWhite : kBlue);
  }
}
}  // namespace
void VoiceOverlay::reset(int64_t now) {
  levels_.fill(0);
  envelope_ = 0;
  began_ = audioAt_ = phaseAt_ = now;
  phase_ = Phase::Starting;
}
void VoiceOverlay::audio(const int16_t* samples, std::size_t count,
                         int64_t now) {
  if (!samples || count == 0 || now < audioAt_) return;
  double sum = 0, squares = 0;
  for (std::size_t i = 0; i < count; ++i) {
    const double sample = samples[i];
    sum += sample;
    squares += sample * sample;
  }
  // Remove DC before measuring; the transport itself remains bit-exact.
  const double mean = sum / count;
  const double rms = std::sqrt(std::max(0., squares / count - mean * mean));
  const float level = static_cast<float>(std::clamp(
      (20. * std::log10(std::max(1., rms) / 32768.) + 48.) / 38., 0., 1.));
  const float elapsed =
      static_cast<float>(std::max<int64_t>(1, now - audioAt_));
  const float weight =
      1.f - std::exp(-elapsed / (level > envelope_ ? 35.f : 180.f));
  envelope_ += (level - envelope_) * weight;
  std::move(levels_.begin() + 1, levels_.end(), levels_.begin());
  levels_.back() = envelope_;
  audioAt_ = now;
}
void VoiceOverlay::draw(Canvas& c, Phase phase, int64_t now) {
  if (phase != phase_) {
    phase_ = phase;
    phaseAt_ = now;
  }
  c.clear(0);
  if (c.width() < TC002_PANEL_WIDTH || c.height() < TC002_PANEL_HEIGHT) return;
  const auto seconds = [now](int64_t from) {
    return static_cast<float>(std::max<int64_t>(0, now - from)) / 1000.f;
  };
  const float u = seconds(began_), since = seconds(phaseAt_);
  const float stale = std::exp(
      -static_cast<float>(std::max<int64_t>(0, now - audioAt_ - 80)) / 180.f);
  const bool blink = std::fmod(u, 2.7f) > 2.58f;
  const bool listening = phase == Phase::Listening;
  if (u < .75f && (listening || phase == Phase::Starting)) {
    wake(c, rise(u), u, listening);
    dots(c, u);
    if (listening)
      bars(c, levels_, stale * std::clamp((u - .3f) / .3f, 0.f, 1.f));
    return;
  }
  switch (phase) {
    case Phase::Starting:
      house(c, 0, 0, kBlue);
      eyes(c, 0, 0, blink ? Eyes::Closed : Eyes::Open);
      mouth(c, 0, 0, Mouth::Smile);
      dots(c, 1);
      break;
    case Phase::Listening: {
      const float envelope = levels_.back() * stale;
      const int oy = envelope > .55f ? -1 : 0;
      house(c, 0, oy, kBlue);
      eyes(c, 0, oy,
           blink               ? Eyes::Closed
           : envelope > .8f    ? Eyes::Tall
                               : Eyes::Open);
      mouth(c, 0, oy, Mouth::Round);
      bars(c, levels_, stale);
      break;
    }
    case Phase::Processing:
      house(c, 0, 0, kBlue);
      eyes(c, 0, 0, blink ? Eyes::Closed : Eyes::Open,
           static_cast<int>(since / 1.1f) % 2 ? -1 : 1, -1);
      mouth(c, 0, 0, Mouth::Flat);
      think(c, since);
      break;
    case Phase::Speaking: {
      house(c, 0, 0, kBlue);
      eyes(c, 0, 0, Eyes::Happy);
      c.setPixel(2, 12, kPink);
      c.setPixel(12, 12, kPink);
      const float open = talk(since);
      mouth(c, 0, 0,
            open > .6f   ? Mouth::Wide
            : open > .2f ? Mouth::Half
                         : Mouth::Flat);
      arcs(c, since);
      break;
    }
    case Phase::Error: {
      const int ox = since < .6f ? nearest(std::sin(since * 38.f) * 1.4f *
                                           (1.f - since / .6f))
                                 : 0;
      house(c, ox, 0, scale(kBlue, .55f));
      eyes(c, ox, 0, Eyes::Cross);
      mouth(c, ox, 0, Mouth::Frown);
      const float drop = std::fmod(since * 7.f, 8.f);
      if (drop < 6) {
        c.setPixel(17, 2 + static_cast<int>(drop), kDrop);
        if (drop > .8f)
          c.setPixel(17, 1 + static_cast<int>(drop), scale(kDrop, .4f));
      }
      if (since > 1.2f || std::fmod(since * 3.f, 1.f) < .65f) {
        c.fillRect(33, 2, 2, 7, kOrange);
        c.fillRect(33, 11, 2, 2, kOrange);
      }
      break;
    }
  }
}
}  // namespace awtrix::tc002::voice
