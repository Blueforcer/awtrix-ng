#pragma once

#include <cstdint>

#include "core/StateStore.h"
#include "platform/tc002/runtime/Tc002Paint.h"
#include "core/render/Canvas.h"
#include "core/render/Font.h"

namespace awtrix {

// Knob controls for brightness and master volume. The first detent opens the screen;
// further detents change 5 %, a short push switches rows, and timeout closes it.
// Brightness follows the moodlight when active. The selected row is not persisted.
class Tc002QuickSettings {
 public:
  static constexpr int64_t kTimeoutMs = 1500;

  explicit Tc002QuickSettings(StateStore& state) : state_(state) {}
  Tc002QuickSettings(const Tc002QuickSettings&) = delete;
  Tc002QuickSettings& operator=(const Tc002QuickSettings&) = delete;

  void turn(int direction, int64_t nowMs);
  // A classified short push opens the screen or switches its selected row.
  void push(int64_t nowMs);
  // Voice takes the panel immediately, including during the closing animation.
  void dismiss();
  // Draws over the app frame already on the canvas; false while the screen is closed.
  bool draw(Canvas& canvas, const GfxFont& font, int64_t nowMs);
  // The screen is up and taking the knob.
  bool open(int64_t nowMs) const { return visible(nowMs); }

 private:
  enum Row { kBrightness = 0, kVolume = 1, kRows = 2 };
  using Fx = paint::ParticleKind;
  static constexpr int64_t kNever = INT64_MIN / 4;
  static constexpr int kEffects = 4;
  struct Motion {
    float from = 0.0f;
    int64_t changedAt = kNever;
    int direction = 0;
    int64_t limitAt = kNever;
    bool limitHigh = false;
    paint::ParticleRing<kEffects> effects;
  };

  int percent(int row) const;
  void setPercent(int row, int value);
  bool visible(int64_t nowMs) const { return active_ && nowMs < lastInputAt_ + kTimeoutMs; }
  void openAt(int64_t nowMs);
  float shownValue(int row, int64_t nowMs) const;
  void addEffect(int row, Fx kind, int64_t at, int x, int count);
  void drawPanel(Canvas& c, const GfxFont& font, int64_t nowMs, bool effects) const;
  void drawRow(Canvas& c, const GfxFont& font, int row, float weight, int dx, int dy, int64_t nowMs) const;
  void drawEffects(Canvas& c, int row, int64_t nowMs) const;
  void drawClosing(Canvas& c, const GfxFont& font, int64_t closeAt, int64_t nowMs);

  StateStore& state_;
  bool active_ = false;
  int64_t lastInputAt_ = 0, selectedAt_ = kNever;
  int selected_ = kBrightness, previous_ = kBrightness, lastChanged_ = kBrightness;
  Motion rows_[kRows];
  Canvas closing_{0, 0};
  int64_t closingFor_ = kNever;
};

}
