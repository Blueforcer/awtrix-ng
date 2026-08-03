#pragma once

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

#include "core/payload/ScrollSpec.h"
#include "core/render/ColorRamp.h"
#include "core/render/Font.h"

namespace awtrix {

enum class TextCase : uint8_t { Inherit, Upper, AsTyped };

enum class IconMode : uint8_t { Fixed, PushOnce, Push };

enum class LifetimeExpiry : uint8_t { Remove, Mark };

enum class DrawKind : uint8_t {
  Pixel, Pixels, Line, Rect, FillRect, Circle, FillCircle, Text, Bitmap
};

struct DrawOp {
  DrawKind kind = DrawKind::Pixel;
  int x = 0, y = 0;
  int x2 = 0, y2 = 0;
  int w = 0, h = 0;
  int r = 0;
  uint32_t color = 0xFFFFFFu;
  bool inheritColor = false;
  std::string text;
  std::vector<uint32_t> bitmap;
  std::vector<int> points;
};

struct TextFragment {
  std::string text;
  uint32_t color = 0xFFFFFFu;
};

// The rarely used half of AppSpec, kept behind a shared pointer so a plain text app stays small.
struct AppSpecExtras {
  render::ColorRamp palette;
  bool textUsesPalette = false;
  bool chartUsesPalette = false;
  bool progressUsesPalette = false;

  std::vector<int> barChart;
  std::vector<int> lineChart;
  bool chartAutoscale = true;
  bool hasChartColor = false;
  uint32_t chartColor = 0u;

  int progress = -1;
  uint32_t progressColor = 0x00FF00u;
  uint32_t progressTrackColor = 0xFFFFFFu;

  float effectSpeed = 1.0f;
  bool hasEffectSpeed = false;
  std::vector<DrawOp> draw;

  std::string rtttl;
};

struct AppSpec {
  std::string name;
  bool isNotification = false;

  std::string text;
  std::vector<TextFragment> fragments;
  TextCase textCase = TextCase::Inherit;
  FontId font = FontId::Small;
  bool textInFront = false;
  bool textCenter = true;
  bool hasTextColor = false;
  uint32_t textColor = 0xFFFFFFu;
  int textBlinkMs = 0;
  int textFadeMs = 0;

  bool hasBackgroundColor = false;
  uint32_t backgroundColor = 0u;
  std::string icon;
  IconMode iconMode = IconMode::Fixed;
  int iconOffsetX = 0;
  int textOffsetX = 0;

  int repeat = 0;
  long durationMs = 0;
  ScrollSpec scroll;
  long lifetimeMs = 0;
  LifetimeExpiry lifetimeExpiry = LifetimeExpiry::Remove;
  bool lifeTimeEnd = false;

  std::string effect;
  std::string overlay;

  bool hold = false;
  bool stack = true;
  bool wakeup = false;
  std::string sound;
  bool loopSound = false;

  const AppSpecExtras& extras() const {
    static const AppSpecExtras kEmpty;
    return extras_ ? *extras_ : kEmpty;
  }
  // Copy on write: allocates on first use and forks the block if another AppSpec copy shares it.
  AppSpecExtras& extrasMut() {
    if (!extras_)
      extras_ = std::make_shared<AppSpecExtras>();
    else if (extras_.use_count() > 1)
      extras_ = std::make_shared<AppSpecExtras>(*extras_);
    return *extras_;
  }

 private:
  std::shared_ptr<AppSpecExtras> extras_;
};

}
