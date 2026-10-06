#pragma once

#include <algorithm>

#include "core/effects/EffectMath.h"
#include "core/effects/EffectNoise.h"
#include "core/effects/IEffect.h"

namespace awtrix {

namespace detail {
// density is a 1-in-N chance that a column is raining on a given pass, tailMin/tailMax the trail
// length in pixels, and slantDiv the horizontal skew where 0 means straight down.
struct RainStyle {
  int density;
  int tailMin;
  int tailMax;
  int slantDiv;
};

// Shared column engine behind drizzle, rain, storm and thunder. pos is a per-column clock and
// pos/span re-rolls the column each time it wraps, giving every pass a fresh tail and gap. Drops
// lighten what is underneath.
inline void rainColumns(Canvas& c, int64_t frame, const RainStyle& st, uint32_t drop, uint32_t tail) {
  const int span = c.height() + 6;
  for (int x = 0; x < c.width(); ++x) {
    const uint32_t colSeed = noise::hash2(static_cast<uint32_t>(x), 0x5241494Eu);
    const int64_t pos = frame + static_cast<int>(colSeed % static_cast<uint32_t>(span));
    const uint32_t roll = noise::hash2(colSeed, static_cast<uint32_t>(pos / span));
    if (roll % static_cast<uint32_t>(st.density) != 0) continue;
    const int fall = static_cast<int>(pos % span);
    const int tailLen =
        st.tailMin + static_cast<int>((roll >> 8) % static_cast<uint32_t>(st.tailMax - st.tailMin + 1));
    for (int t = 0; t <= tailLen; ++t) {
      const int y = fall - 2 - t;
      if (y < 0 || y >= c.height()) continue;
      const int dx = st.slantDiv ? (fall - t) / st.slantDiv : 0;
      fx::lighten(c, (x + dx) % c.width(), y, t == 0 ? drop : tail);
    }
  }
}
}

class DrizzleOverlay : public IEffect {
 public:
  const std::string& id() const override { return id_; }
  float rate() const override { return rate::kGentle; }
  void render(Canvas& c, int64_t frame) override {
    detail::rainColumns(c, frame, {6, 0, 1, 0}, paletteColor(200, 0x2255AAu),
                        paletteColor(60, 0x112244u));
  }
 private:
  std::string id_ = "drizzle";
};

class StormOverlay : public IEffect {
 public:
  const std::string& id() const override { return id_; }
  float rate() const override { return rate::kBrisk; }
  void render(Canvas& c, int64_t frame) override {
    detail::rainColumns(c, frame, {2, 2, 3, 3}, paletteColor(200, 0x0033AAu),
                        paletteColor(60, 0x001133u));
  }
 private:
  std::string id_ = "storm";
};

class ThunderOverlay : public IEffect {
 public:
  const std::string& id() const override { return id_; }
  float rate() const override { return rate::kBrisk; }
  void render(Canvas& c, int64_t frame) override {
    detail::rainColumns(c, frame, {2, 2, 3, 3}, paletteColor(200, 0x0033AAu),
                        paletteColor(60, 0x001133u));
    // At most one strike per 30-frame window. A strike is a jagged bolt with one side branch; the
    // sky lights up with it and dies away, and some strikes flare a second time.
    const uint32_t roll = noise::hash2(static_cast<uint32_t>(frame / kFlashWindow), 0x424F4C54u);
    if (roll % 3u) return;
    static const uint8_t kLight[6] = {255, 110, 40, 200, 80, 25};
    const int tick = static_cast<int>(frame % kFlashWindow);
    if (tick >= ((roll & 4u) ? 6 : 3)) return;
    const uint32_t light = paletteColor(255, 0xFFFFFFu);
    const uint32_t sky = fx::dim(light, static_cast<uint8_t>(kLight[tick] * 3 / 5));
    for (int y = 0; y < c.height(); ++y)
      for (int x = 0; x < c.width(); ++x) fx::lighten(c, x, y, sky);
    const uint32_t bolt = fx::dim(light, static_cast<uint8_t>(std::min(255, kLight[tick] + 80)));
    int x = static_cast<int>((roll >> 8) % static_cast<uint32_t>(c.width()));
    const int fork = c.height() / 3;
    int bx = x;
    for (int y = 0; y < c.height(); ++y) {
      x += static_cast<int>(noise::hash2(roll + static_cast<uint32_t>(y), 0x5A41u) % 3u) - 1;
      c.setPixel(x, y, bolt);
      if (y == fork) bx = x;
      if (y > fork && y <= 2 * fork) {
        bx += (roll & 8u) ? 1 : -1;
        c.setPixel(bx, y, fx::dim(bolt, 150));
      }
    }
  }
 private:
  static constexpr int64_t kFlashWindow = 30;
  std::string id_ = "thunder";
};

class FrostOverlay : public IEffect {
 public:
  const std::string& id() const override { return id_; }
  float rate() const override { return rate::kGentle; }
  // The ice is a fixed hash of x, so the rim keeps its shape; each crystal shimmers on its own
  // slow cycle and now and then glints white. The rim grows with the panel height and leaves the
  // middle rows free for the text.
  void render(Canvas& c, int64_t frame) override {
    const uint32_t ice = paletteColor(220, 0x88CCFFu);
    const uint32_t creep = paletteColor(140, 0x446688u);
    const int depth = std::max(1, c.height() / 8);
    for (int x = 0; x < c.width(); ++x) {
      const uint32_t top = noise::hash2(static_cast<uint32_t>(x), 0x46524F53u);
      const uint32_t bot = noise::hash2(static_cast<uint32_t>(x), 0x54465254u);
      for (int d = 0; d < depth; ++d) {
        if (top % 3u) crystal(c, x, d, ice, frame);
        if ((top >> 8) % 5u == 0) crystal(c, x, depth + d, creep, frame);
        if (bot % 3u) crystal(c, x, c.height() - 1 - d, ice, frame);
        if ((bot >> 8) % 5u == 0) crystal(c, x, c.height() - 1 - depth - d, creep, frame);
      }
    }
  }
 private:
  static void crystal(Canvas& c, int x, int y, uint32_t col, int64_t frame) {
    const uint32_t h = noise::hash2(static_cast<uint32_t>(y * 131 + x), 0x474C494Eu);
    const int phase = static_cast<int>((frame + h % 256u) % 256);
    if (phase < 3 && (h >> 8) % 4u == 0) {
      c.setPixel(x, y, 0xFFFFFFu);
      return;
    }
    const int wave = phase < 128 ? phase : 255 - phase;
    c.setPixel(x, y, fx::dim(col, static_cast<uint8_t>(165 + wave * 90 / 127)));
  }
  std::string id_ = "frost";
};

}
