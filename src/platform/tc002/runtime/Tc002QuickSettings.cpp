#include "platform/tc002/runtime/Tc002QuickSettings.h"

#include <algorithm>
#include <cmath>
#include <string>

#include "core/render/Color.h"
#include "core/launcher/MenuStyle.h"
#include "core/render/Motion.h"
#include "core/render/TextRenderer.h"
#include "platform/tc002/runtime/Tc002Paint.h"

namespace awtrix {
namespace {

using motion::bounce;
using motion::clamp01;
using motion::easeOut;
using motion::noise;
using motion::progress;
using motion::pulse;
using paint::plot;
using paint::scale;

using launcher::style::kAmber;
using launcher::style::kCyan;
constexpr uint32_t kMoonlight = 0xD8E4FFu;
using launcher::style::kTrack;
constexpr uint32_t kRowColor[2] = {kAmber, kCyan};

constexpr int kRowY[2] = {1, 10};
constexpr int kIconX = 2;
constexpr int kBarX = 9;
constexpr int kBarW = 30;
constexpr int kStep = 5;
constexpr int kMinPercent[2] = {5, 0};

constexpr float kChangeMs = 150;
constexpr float kFadeMs = 150;
constexpr float kHopMs = 250;
constexpr int64_t kLimitMs = 150;
constexpr int64_t kBreathAfterMs = 800;
constexpr int64_t kBreathMs = 1100;
constexpr int64_t kCloseMs = 450;
using paint::kShake;

constexpr const char* kMoon[] = {".###.", "##...", "##...", "##...", ".###."};
constexpr const char* kSunSmall[] = {".....", ".###.", ".###.", ".###.", "....."};
constexpr const char* kSunMid[] = {"..#..", ".###.", "#####", ".###.", "..#.."};
constexpr const char* kSunLarge[] = {"#.#.#", ".###.", "#####", ".###.", "#.#.#"};
constexpr const char* kSpeaker[] = {"..#", "###", "###", "###", "..#"};
constexpr const char* kMute[] = {"#.#", ".#.", "#.#"};


using launcher::style::sprite;

}

int Tc002QuickSettings::percent(int row) const {
  if (row == kVolume) return state_.settings().volume;
  const RuntimeState& runtime = state_.runtime();
  const int level = runtime.moodlightMode ? runtime.moodlightBrightness : state_.settings().brightness;
  return (std::clamp(level, 0, 255) * 100 + 127) / 255;
}

void Tc002QuickSettings::setPercent(int row, int value) {
  if (row == kVolume) {
    state_.settings().volume = value;
    state_.emit(StateEvent::SettingsChanged);
    return;
  }
  const int level = (value * 255 + 50) / 100;
  RuntimeState& runtime = state_.runtime();
  if (runtime.moodlightMode) {
    runtime.moodlightBrightness = static_cast<uint8_t>(level);
    state_.emit(StateEvent::MoodlightChanged);
  } else {
    state_.settings().brightness = level;
    state_.emit(StateEvent::SettingsChanged);
  }
}

void Tc002QuickSettings::openAt(int64_t now) {
  active_ = true;
  lastInputAt_ = now;
  selected_ = previous_ = lastChanged_;
  selectedAt_ = kNever;
  for (int row = 0; row < kRows; ++row) {
    rows_[row] = Motion{};
    rows_[row].from = static_cast<float>(percent(row));
  }
}

void Tc002QuickSettings::turn(int direction, int64_t now) {
  if (state_.runtime().matrixOff || direction == 0) return;
  if (!visible(now)) {
    openAt(now);
    return;
  }
  lastInputAt_ = now;
  direction = direction > 0 ? 1 : -1;
  const int row = selected_;
  Motion& m = rows_[row];
  const int current = percent(row);
  const int next = std::clamp((current + kStep / 2) / kStep * kStep + direction * kStep, kMinPercent[row], 100);
  if (direction > 0 ? next <= current : next >= current) {
    m.limitAt = now;
    m.limitHigh = direction > 0;
    if (m.limitHigh) addEffect(row, Fx::Burst, now, kBarX + kBarW - 1, 0);
    return;
  }
  const float before = shownValue(row, now);
  const int fillBefore = static_cast<int>(std::lround(before * kBarW / 100.0f));
  m.from = before;
  m.changedAt = now;
  m.direction = direction;
  setPercent(row, next);
  lastChanged_ = row;
  const int fillAfter = static_cast<int>(std::lround(next * kBarW / 100.0f));
  if (direction > 0) addEffect(row, Fx::Spark, now, kBarX + fillAfter - 1, 0);
  else if (fillBefore > fillAfter) addEffect(row, Fx::Drop, now, kBarX + fillAfter, fillBefore - fillAfter);
}

void Tc002QuickSettings::push(int64_t now) {
  if (state_.runtime().matrixOff) return;
  if (!visible(now)) {
    openAt(now);
    return;
  }
  previous_ = selected_;
  selected_ = (selected_ + 1) % kRows;
  selectedAt_ = now;
  lastInputAt_ = now;
}

void Tc002QuickSettings::dismiss() {
  active_ = false;
  closingFor_ = kNever;
}

float Tc002QuickSettings::shownValue(int row, int64_t now) const {
  const Motion& m = rows_[row];
  const float target = static_cast<float>(percent(row));
  const float t = progress(now, m.changedAt, kChangeMs);
  return t >= 1.0f ? target : m.from + (target - m.from) * easeOut(t / 0.6f);
}

void Tc002QuickSettings::addEffect(int row, Fx kind, int64_t at, int x, int count) {
  Motion& m = rows_[row];
  m.effects.add({kind, at, x, kRowY[row] + (kind == Fx::Burst ? 2 : 1), count, kRowColor[row]});
}

bool Tc002QuickSettings::draw(Canvas& c, const GfxFont& font, int64_t now) {
  if (!active_) return false;
  const int64_t closeAt = lastInputAt_ + kTimeoutMs;
  if (state_.runtime().matrixOff || now >= closeAt + kCloseMs) {
    active_ = false;
    return false;
  }
  if (now >= closeAt) {
    drawClosing(c, font, closeAt, now);
    return true;
  }
  c.clear(color::kBlack);
  drawPanel(c, font, now, true);
  return true;
}

// The screen rains off the panel pixel by pixel while the app fades in behind it.
void Tc002QuickSettings::drawClosing(Canvas& c, const GfxFont& font, int64_t closeAt, int64_t now) {
  if (closing_.width() != c.width() || closing_.height() != c.height()) {
    closing_ = Canvas(c.width(), c.height());
    closingFor_ = kNever;
  }
  if (!closing_.valid()) return;
  if (closingFor_ != closeAt) {
    closing_.clear(color::kBlack);
    drawPanel(closing_, font, closeAt, false);
    closingFor_ = closeAt;
  }
  const float age = static_cast<float>(now - closeAt);
  const float app = easeOut((age - 100.0f) / 350.0f);
  for (int y = 0; y < c.height(); ++y)
    for (int x = 0; x < c.width(); ++x) c.setPixel(x, y, scale(c.getPixel(x, y), app));
  for (int y = 0; y < c.height(); ++y)
    for (int x = 0; x < c.width(); ++x) {
      const uint32_t rgb = closing_.getPixel(x, y);
      if (!rgb) continue;
      const int salt = y * c.width() + x;
      const float falling = age - 200.0f * noise(closeAt, salt);
      if (falling < 0.0f) { c.setPixel(x, y, rgb); continue; }
      const float drift = (noise(closeAt + 1, salt) - 0.5f) * 0.012f * falling;
      plot(c, x + drift, y + 0.0004f * falling * falling, scale(rgb, 1.0f - 0.4f * clamp01(falling / 250.0f)));
    }
}

void Tc002QuickSettings::drawPanel(Canvas& c, const GfxFont& font, int64_t now, bool effects) const {
  const int64_t selectAge = now - selectedAt_;
  const bool switching = selected_ != previous_;
  const float fade = easeOut(selectAge / kFadeMs);
  int dy[kRows];
  for (int row = 0; row < kRows; ++row) {
    const Motion& m = rows_[row];
    const float weight = launcher::style::selectionLight(row, selected_, previous_, switching ? fade : 1.0f);
    int dx = 0;
    dy[row] = 0;
    const int64_t limitAge = now - m.limitAt;
    if (limitAge >= 0 && limitAge < kLimitMs) {
      if (m.limitHigh) dy[row] -= limitAge < kLimitMs / 2 ? 1 : 0;
      else dx = kShake[limitAge / 25];
    }
    if (switching && row == selected_ && selectAge >= 100 && selectAge < 175) dy[row] -= 1;
    drawRow(c, font, row, weight, dx, dy[row], now);
  }
  if (effects)
    for (int row = 0; row < kRows; ++row) drawEffects(c, row, now);

  const float t = switching ? bounce(selectAge / kHopMs) : 1.0f;
  const int from = kRowY[previous_], to = kRowY[selected_];
  const int y = static_cast<int>(std::lround(from + (to - from) * t)) + dy[t < 0.5f ? previous_ : selected_];
  uint32_t marker = color::lerp(kRowColor[previous_], kRowColor[selected_], t);
  const int64_t idle = now - lastInputAt_;
  if (idle > kBreathAfterMs) marker = scale(marker, motion::breathe(idle - kBreathAfterMs, kBreathMs, 0.55f));
  c.fillRect(0, y, 1, 5, marker);
}

void Tc002QuickSettings::drawRow(Canvas& c, const GfxFont& font, int row, float weight, int dx, int dy,
                                 int64_t now) const {
  const Motion& m = rows_[row];
  const int y = kRowY[row] + dy;
  const uint32_t ink = scale(kRowColor[row], weight);
  const float value = shownValue(row, now);
  const int shown = std::clamp(static_cast<int>(std::lround(value)), 0, 100);
  const float change = progress(now, m.changedAt, kChangeMs);
  const int overshoot = change >= 0.3f && change < 0.7f ? m.direction : 0;
  const int fill = std::clamp(static_cast<int>(std::lround(value * kBarW / 100.0f)) + overshoot, 0, kBarW);
  const float head = pulse(now, m.changedAt, kChangeMs);
  const float flash = 0.6f * pulse(now, m.changedAt, 130.0f);
  const float limit = pulse(now, m.limitAt, kLimitMs);

  if (row == kBrightness) {
    const uint32_t moon = paint::flash(scale(color::lerp(kAmber, kMoonlight, 0.75f), weight), flash);
    const uint32_t sun = paint::flash(ink, flash);
    if (shown <= 25) sprite(c, kIconX + dx, y, kMoon, moon);
    else if (shown <= 50) sprite(c, kIconX + dx, y, kSunSmall, sun);
    else if (shown <= 80 || (shown == 100 && now / 350 % 2)) sprite(c, kIconX + dx, y, kSunMid, sun);
    else sprite(c, kIconX + dx, y, kSunLarge, sun);
  } else {
    const uint32_t icon = paint::flash(ink, flash);
    sprite(c, kIconX + dx, y, kSpeaker, icon);
    if (shown == 0) {
      sprite(c, kIconX + 3 + dx, y + 1, kMute, icon);
    } else {
      const int height = shown <= 33 ? 1 : shown <= 66 ? 3 : 5;
      c.fillRect(kIconX + 4 + dx, y + 2 - height / 2, 1, height, icon);
      const float ripple = m.direction > 0 ? pulse(now, m.changedAt, kChangeMs) : 0.0f;
      if (ripple > 0.0f) c.fillRect(kIconX + 5 + dx, y, 1, 5, scale(ink, 0.8f * ripple));
    }
  }

  const uint32_t track = weight > 0.99f ? kTrack : scale(kTrack, 0.75f);
  for (int i = 0; i < kBarW; ++i) {
    uint32_t rgb = i < fill ? ink : track;
    if (i == fill - 1) rgb = paint::flash(rgb, head);
    c.fillRect(kBarX + i + dx, y + 1, 1, 3, rgb);
  }
  if (limit > 0.0f) {
    const int edge = m.limitHigh ? kBarX + kBarW - 1 : kBarX;
    c.fillRect(edge + dx, y, 1, 5, paint::flash(m.limitHigh ? ink : scale(kRowColor[row], 0.6f * weight), limit));
  }

  const std::string digits = std::to_string(shown);
  paint::drawInk(c, font, c.width() - text::measure(font, digits).inkWidth() + dx, y, digits,
                 paint::flash(ink, std::max(flash, 0.5f * limit)));
}

void Tc002QuickSettings::drawEffects(Canvas& c, int row, int64_t now) const {
  paint::drawParticles<paint::ParticleStyle::QuickSettings>(c, rows_[row].effects, now);
}

}
