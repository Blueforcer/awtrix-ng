#include "core/render/BootScreen.h"

#include <algorithm>
#include <cmath>
#include <memory>

#include "core/render/Color.h"
#include "core/render/ColorRamp.h"
#include "core/render/TextRenderer.h"

namespace awtrix {
namespace render {

namespace {
constexpr int kBaselineY = 6;
constexpr long kScrollPxPerSec = 30;
constexpr float kPi = 3.14159265f;
const char kLogo[] = "AWTRIX";

constexpr int64_t kColumnSweepMs = 500;
constexpr int64_t kJitterMs = 200;
constexpr int64_t kFlightMs = 800;
constexpr int64_t kHoldMs = 450;
constexpr int64_t kDropMs = 240;
constexpr int64_t kIgniteMs = 400;
constexpr int64_t kRiseEndMs = kColumnSweepMs + kJitterMs + kFlightMs;
constexpr int64_t kHoldEndMs = kRiseEndMs + kHoldMs;
constexpr int64_t kDropEndMs = kHoldEndMs + kDropMs;
static_assert(kDropEndMs + kIgniteMs == kBootIntroMs, "intro phases must add up");

constexpr int kNgGlyphW = 5;
constexpr int kNgGapW = 1;
constexpr int kNgHeight = 8;
constexpr int kNgWidth = 2 * kNgGlyphW + kNgGapW;
constexpr uint8_t kGlyphN[kNgHeight] = {0b10001, 0b11001, 0b11001, 0b10101,
                                        0b10101, 0b10011, 0b10011, 0b10001};
constexpr uint8_t kGlyphG[kNgHeight] = {0b01110, 0b10001, 0b10000, 0b10000,
                                        0b10111, 0b10001, 0b10001, 0b01110};
constexpr int kAddressGapPx = 12;

constexpr float kOvershootPx = 1.0f;
constexpr float kOvershootFrom = 0.6f;
constexpr uint8_t kTrailScale = 90;
constexpr float kTrailLead = 0.07f;

constexpr int kRampSpanPx = 28;
constexpr float kRampSpeed = 0.5f;

constexpr int kStarCount = 18;
constexpr uint8_t kStarPeak = 70;
constexpr int64_t kStarPeriodMs = 1300;
constexpr int64_t kStarPeriodStepMs = 370;

constexpr int64_t kBoomMs = 400;
static_assert(kBoomMs <= kIgniteMs, "the shockwave has to be over before the address scroll");
constexpr float kBoomMaxR = 5.5f;
constexpr float kBoomThickness = 1.1f;
constexpr float kBoomXScale = 0.25f;

const ColorRamp& logoRamp() {
  static const ColorRamp ramp = [] {
    ColorRamp r;
    r.pal = std::make_shared<Palette>(namedPalette("Rainbow"));
    r.blend = true;
    r.spanPx = kRampSpanPx;
    r.speed = kRampSpeed;
    return r;
  }();
  return ramp;
}

uint32_t rampAtColumn(int col, int origin) {
  int p = (col + origin) % kRampSpanPx;
  if (p < 0) p += kRampSpanPx;
  return logoRamp().atIndex(static_cast<uint8_t>((p * 256) / kRampSpanPx));
}

int ngRestX(const Canvas& c) { return (c.width() - kNgWidth) / 2; }

int ngRestY(const Canvas& c) { return (c.height() - kNgHeight) / 2; }

int logoX(const Canvas& c, const GfxFont& font) {
  return (c.width() - text::width(font, kLogo)) / 2;
}

int64_t elapsed(int64_t startMs, int64_t nowMs) { return nowMs > startMs ? nowMs - startMs : 0; }

// When the spark headed for (x, y) sets off: a left-to-right sweep across the logo's ink plus a
// deterministic per-pixel jitter, so the columns don't all launch in lockstep.
int64_t spawnDelay(int x, int y, int inkLeft, int inkRight) {
  const int span = inkRight > inkLeft ? inkRight - inkLeft : 1;
  const int col = x - inkLeft;
  return static_cast<int64_t>(col) * kColumnSweepMs / span + (x * 7 + y * 13) % (kJitterMs + 1);
}

float easeOutCubic(float t) {
  const float u = 1.0f - t;
  return 1.0f - u * u * u;
}

float overshootPx(float t) {
  if (t <= kOvershootFrom) return 0.0f;
  const float k = (t - kOvershootFrom) / (1.0f - kOvershootFrom);
  return kOvershootPx * std::sin(kPi * k);
}

uint32_t sparkColor(float t) {
  static const Palette& heat = namedPalette("Heat");
  return colorFromPalette(heat, static_cast<uint8_t>(40.0f + 200.0f * t), true);
}

uint32_t scaled(uint32_t rgb, uint8_t scale) {
  return color::pack(color::scale8(color::red(rgb), scale),
                     color::scale8(color::green(rgb), scale),
                     color::scale8(color::blue(rgb), scale));
}

int luma(uint32_t c) { return color::red(c) + color::green(c) + color::blue(c); }

// Sparks, trails and stars overlap constantly, so keep the brighter pixel instead of whichever
// happened to be drawn last.
void plotBrightest(Canvas& c, int x, int y, uint32_t rgb) {
  if (luma(rgb) > luma(c.getPixel(x, y))) c.setPixel(x, y, rgb);
}

void renderLogo(Canvas& out, const GfxFont& font) {
  out.clear(color::kBlack);
  text::drawText(out, font, logoX(out, font), kBaselineY, kLogo, 0xFFFFFFu);
}

struct Point {
  float x;
  float y;
};

// Position of one spark at p (0..1) of its flight, easing into its target pixel and overshooting
// slightly outward near the end so it settles back rather than stopping dead.
Point sparkAt(float p, float spawnX, float spawnY, int targetX, int targetY) {
  const float e = easeOutCubic(p);
  const float over = overshootPx(p);
  const float dx = static_cast<float>(targetX) - spawnX;
  const float dir = dx > 0.0f ? 1.0f : (dx < 0.0f ? -1.0f : 0.0f);
  return {spawnX + dx * e + over * dir,
          spawnY + (static_cast<float>(targetY) - spawnY) * e - over};
}

void plotSpark(Canvas& c, Point at, uint32_t rgb) {
  plotBrightest(c, static_cast<int>(std::lround(at.x)), static_cast<int>(std::lround(at.y)), rgb);
}

void drawRise(Canvas& c, const GfxFont& font, int64_t t) {
  Canvas targets(c.width(), c.height());
  renderLogo(targets, font);

  const text::TextMetrics m = text::measure(font, kLogo);
  const int inkLeft = logoX(c, font) + m.inkLeft;
  const int inkRight = logoX(c, font) + m.inkRight;

  const float spawnY = static_cast<float>(c.height());
  for (int x = 0; x < c.width(); ++x) {
    for (int y = 0; y < c.height(); ++y) {
      if (!targets.getPixel(x, y)) continue;
      const int64_t delay = spawnDelay(x, y, inkLeft, inkRight);
      if (t < delay) continue;
      const float p = std::min(1.0f, static_cast<float>(t - delay) / static_cast<float>(kFlightMs));
      const float spawnX = static_cast<float>((x * 13 + y * 29 + 7) % c.width());
      const uint32_t rgb = sparkColor(p);
      const uint8_t trail = static_cast<uint8_t>(kTrailScale * (1.0f - p));
      if (trail)
        plotSpark(c, sparkAt(std::max(0.0f, p - kTrailLead), spawnX, spawnY, x, y),
                  scaled(rgb, trail));
      plotSpark(c, sparkAt(p, spawnX, spawnY, x, y), rgb);
    }
  }
}

template <typename Fn>
void forEachNgPixel(int x0, int yTop, Fn plot) {
  for (int i = 0; i < 2; ++i) {
    const uint8_t* rows = i == 0 ? kGlyphN : kGlyphG;
    const int gx = x0 + i * (kNgGlyphW + kNgGapW);
    for (int r = 0; r < kNgHeight; ++r)
      for (int col = 0; col < kNgGlyphW; ++col)
        if (rows[r] & (1u << (kNgGlyphW - 1 - col))) plot(gx + col, yTop + r);
  }
}

void drawNgGradient(Canvas& c, int x0, int yTop, int rampOrigin) {
  forEachNgPixel(x0, yTop,
                 [&](int x, int y) { plotBrightest(c, x, y, rampAtColumn(x - x0, rampOrigin)); });
}

void drawNgFlat(Canvas& c, int x0, int yTop, uint32_t rgb) {
  forEachNgPixel(x0, yTop, [&](int x, int y) { plotBrightest(c, x, y, rgb); });
}

// Squashes the AWTRIX wordmark down toward its own bottom row as the NG block lands on it.
// ngLowestRow is the block's bottom row: far above the text leaves it untouched, level flattens it.
void drawCrushedLogo(Canvas& c, const GfxFont& font, int ngLowestRow) {
  Canvas src(c.width(), c.height());
  renderLogo(src, font);

  int top = -1, bottom = -1;
  for (int y = 0; y < c.height(); ++y)
    for (int x = 0; x < c.width(); ++x)
      if (src.getPixel(x, y)) {
        if (top < 0) top = y;
        bottom = y;
      }
  if (top < 0) return;

  float k = static_cast<float>(bottom - ngLowestRow) / static_cast<float>(bottom - top + 1);
  if (k > 1.0f) k = 1.0f;
  if (k <= 0.0f) return;
  const uint8_t v = static_cast<uint8_t>(255.0f * std::sqrt(k));
  if (!v) return;

  for (int y = top; y <= bottom; ++y) {
    const int dy = bottom - static_cast<int>(std::lround((bottom - y) * k));
    for (int x = 0; x < c.width(); ++x)
      if (src.getPixel(x, y)) plotBrightest(c, x, dy, color::pack(v, v, v));
  }
}

void drawStandingLogo(Canvas& c, const GfxFont& font) { drawCrushedLogo(c, font, -kNgHeight); }

void drawDrop(Canvas& c, const GfxFont& font, int64_t since) {
  const float p = static_cast<float>(since) / static_cast<float>(kDropMs);
  const float from = -static_cast<float>(kNgHeight - 1);
  const float travel = static_cast<float>(ngRestY(c)) - from;
  const int yTop = static_cast<int>(std::lround(from + travel * p * p));
  drawCrushedLogo(c, font, yTop + kNgHeight - 1);
  drawNgFlat(c, ngRestX(c), yTop, 0xFFFFFFu);
}

void igniteBloom(Canvas& c, float k) {
  for (int x = 0; x < c.width(); ++x)
    for (int y = 0; y < c.height(); ++y) {
      const uint32_t rgb = c.getPixel(x, y);
      if (rgb) c.setPixel(x, y, color::lerp(rgb, 0xFFFFFFu, k));
    }
}

// Shockwave ring expanding from the centre. x is scaled down so the circle reads as a wide ellipse
// on a panel four times wider than it is tall.
void drawBoom(Canvas& c, float p) {
  const float cx = static_cast<float>(c.width() - 1) * 0.5f;
  const float cy = static_cast<float>(c.height() - 1) * 0.5f;
  const float r = kBoomMaxR * easeOutCubic(p);
  const float half = kBoomThickness * 0.5f;
  const float fade = 1.0f - p;

  for (int x = 0; x < c.width(); ++x) {
    for (int y = 0; y < c.height(); ++y) {
      const float dx = (static_cast<float>(x) - cx) * kBoomXScale;
      const float dy = static_cast<float>(y) - cy;
      const float off = std::fabs(std::sqrt(dx * dx + dy * dy) - r);
      if (off >= half) continue;
      const uint8_t v = static_cast<uint8_t>(255.0f * (1.0f - off / half) * fade);
      if (v) plotBrightest(c, x, y, color::pack(v, v, v));
    }
  }
}

void drawStars(Canvas& c, int64_t nowMs) {
  if (c.width() <= 0 || c.height() <= 0) return;
  for (int i = 0; i < kStarCount; ++i) {
    const int x = (i * 11 + 3) % c.width();
    const int y = (i * 5 + 1) % c.height();
    if (c.getPixel(x, y)) continue;
    const int64_t period = kStarPeriodMs + i * kStarPeriodStepMs;
    const float phase = static_cast<float>((nowMs + i * 613) % period) / static_cast<float>(period);
    const float s = std::sin(2.0f * kPi * phase);
    if (s <= 0.0f) continue;
    const uint8_t v = static_cast<uint8_t>(kStarPeak * s * s * s);
    if (v) c.setPixel(x, y, color::pack(v, v, v));
  }
}
}

// The intro in order: sparks fly up and assemble the AWTRIX wordmark, it holds, the NG block drops
// onto it and crushes it, then the block ignites and a shockwave rings out.
void drawBootLogo(Canvas& c, const GfxFont& font, int64_t startMs, int64_t nowMs) {
  const int64_t t = elapsed(startMs, nowMs);
  c.clear(color::kBlack);
  if (t < kRiseEndMs) {
    drawRise(c, font, t);
  } else if (t < kHoldEndMs) {
    drawStandingLogo(c, font);
  } else if (t < kDropEndMs) {
    drawDrop(c, font, t - kHoldEndMs);
  } else {
    const int64_t sinceImpact = t - kDropEndMs;
    drawNgGradient(c, ngRestX(c), ngRestY(c), logoRamp().originAt(nowMs, kRampSpanPx));
    if (t < kBootIntroMs)
      igniteBloom(c, 1.0f - static_cast<float>(sinceImpact) / static_cast<float>(kIgniteMs));
    if (sinceImpact < kBoomMs)
      drawBoom(c, static_cast<float>(sinceImpact) / static_cast<float>(kBoomMs));
  }
  drawStars(c, nowMs);
}

bool drawBootAddress(Canvas& c, const GfxFont& font, const std::string& address, int64_t startMs,
                     int64_t nowMs) {
  const ColorRamp& ramp = logoRamp();
  const int origin = ramp.originAt(nowMs, kRampSpanPx);
  const int tail = kNgWidth + kAddressGapPx;
  const int x = ngRestX(c) - static_cast<int>(elapsed(startMs, nowMs) * kScrollPxPerSec / 1000);

  c.clear(color::kBlack);
  drawNgGradient(c, x, ngRestY(c), origin);

  text::TextPaint paint;
  paint.ramp = &ramp;
  paint.rampOriginPx = origin + tail;
  text::drawRun(c, font, static_cast<float>(x + tail), kBaselineY, address, paint);

  drawStars(c, nowMs);
  return x + tail + text::width(font, address) >= 0;
}

}
}
