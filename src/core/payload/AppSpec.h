#pragma once

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

#include "core/payload/ScrollSpec.h"
#include "core/memory/CheckedShared.h"
#include "core/render/ColorRamp.h"
#include "core/render/DrawProgram.h"
#include "core/render/Font.h"
#include "core/render/TextRenderer.h"

namespace awtrix {
class PageContent;

enum class TextCase : uint8_t { Inherit, Upper, AsTyped };
inline constexpr const char* kTextCaseNames[] = {"inherit", "upper", "asTyped"};

enum class Align : uint8_t { Start, Center, End };
inline constexpr const char* kAlignNames[] = {"start", "center", "end"};

inline int aligned(Align align, int origin, int available, int used) {
  return origin + (align == Align::Center ? (available - used) / 2 :
                   align == Align::End ? available - used : 0);
}

enum class IconMode : uint8_t { Fixed, PushOnce, Push };

enum class LifetimeExpiry : uint8_t { Remove, Mark };

inline constexpr std::size_t kMaxPlacedIcons = 4;

struct PlacedIconSpec {
  std::string icon;
  int x = 0;
  int y = 0;
};

// The rarely used half of AppSpec, kept behind a shared pointer so a plain text app stays small.
struct AppSpecExtras {
  std::shared_ptr<PageContent> content;
  std::vector<PlacedIconSpec> icons;
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
  render::DrawProgram draw;

  // The script whose own folder `sound` is looked for in first; empty for a notification that
  // came from outside.
  std::string soundScript;
};

struct AppSpec {
  std::string name;
  bool isNotification = false;

  std::string text;
  // Set when text came as a list of fragments: one coloured run per fragment over text.
  std::vector<text::TextRun> fragments;
  TextCase textCase = TextCase::Inherit;
  // A font name from the catalog; empty means small.
  std::string font;
  bool textInFront = false;
  Align textAlign = Align::Center;
  bool hasTextColor = false;
  uint32_t textColor = 0xFFFFFFu;
  int textBlinkMs = 0;
  int textFadeMs = 0;

  bool hasBackgroundColor = false;
  uint32_t backgroundColor = 0u;
  std::string icon;
  IconMode iconMode = IconMode::Fixed;
  int iconOffsetX = 0;
  int iconGap = 1;
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
  // The notification's sound object as sent; empty for none.
  std::string sound;

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

  // The extras to fill in, or null when they are shared or cannot be allocated.
  AppSpecExtras* tryExtrasMut() {
    if (extras_ && extras_.use_count() > 1) return nullptr;
    if (!extras_) extras_ = checked::tryMakeShared<AppSpecExtras>();
    return extras_.get();
  }

 private:
  std::shared_ptr<AppSpecExtras> extras_;
};

}
