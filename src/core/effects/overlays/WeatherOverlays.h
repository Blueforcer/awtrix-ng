#pragma once

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
// pos/span re-rolls the column each time it wraps, giving every pass a fresh tail and gap.
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
      c.setPixel((x + dx) % c.width(), y, t == 0 ? drop : tail);
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
    // At most one strike per 30-frame window, and sometimes a second flash three frames later.
    const uint32_t roll =
        noise::hash2(static_cast<uint32_t>(frame / kFlashWindow), 0x424F4C54u);
    const int64_t tick = frame % kFlashWindow;
    const bool strikes = roll % 3u == 0;
    if (strikes && (tick == 0 || (tick == 3 && (roll & 4u)))) {
      const uint32_t flash = paletteColor(255, 0xFFFFFFu);
      for (int y = 0; y < c.height(); ++y)
        for (int x = 0; x < c.width(); ++x) c.setPixel(x, y, flash);
      return;
    }
    detail::rainColumns(c, frame, {2, 2, 3, 3}, paletteColor(200, 0x0033AAu),
                        paletteColor(60, 0x001133u));
  }
 private:
  static constexpr int64_t kFlashWindow = 30;
  std::string id_ = "thunder";
};

class FrostOverlay : public IEffect {
 public:
  const std::string& id() const override { return id_; }
  float rate() const override { return rate::kStatic; }
  // Ignores the frame counter: the ice is a pure hash of x, so the pattern is fixed until reboot.
  void render(Canvas& c, int64_t) override {
    const uint32_t ice = paletteColor(220, 0x88CCFFu);
    const uint32_t creep = paletteColor(140, 0x446688u);
    for (int x = 0; x < c.width(); ++x) {
      const uint32_t top = noise::hash2(static_cast<uint32_t>(x), 0x46524F53u);
      const uint32_t bot = noise::hash2(static_cast<uint32_t>(x), 0x54465254u);
      if (top % 3u) c.setPixel(x, 0, ice);
      if ((top >> 8) % 5u == 0) c.setPixel(x, 1, creep);
      if (bot % 3u) c.setPixel(x, c.height() - 1, ice);
      if ((bot >> 8) % 5u == 0) c.setPixel(x, c.height() - 2, creep);
    }
  }
 private:
  std::string id_ = "frost";
};

}
