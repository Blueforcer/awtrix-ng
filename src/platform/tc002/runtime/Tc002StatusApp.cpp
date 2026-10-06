#include "platform/tc002/runtime/Tc002StatusApp.h"

#include <algorithm>
#include <cmath>
#include <string_view>

#include "core/render/Color.h"
#include "core/apps/SensorFormat.h"
#include "core/net/SignalStrength.h"
#include "core/launcher/MenuStyle.h"
#include "core/render/Motion.h"
#include "core/render/TextRenderer.h"
#include "platform/tc002/runtime/Tc002Paint.h"

namespace awtrix {
namespace {

using namespace motion;
using paint::drawInk;
using paint::plot;
using paint::scale;
using paint::whiten;

constexpr uint32_t kShell = 0x787882u;
constexpr uint32_t kAmber = 0xFFA000u;
constexpr uint32_t kRed = 0xFF2000u;
using launcher::style::kCyan;
constexpr uint32_t kWhite = 0xFFFFFFu;
constexpr uint32_t kBoltColor = 0xFFE600u;
constexpr uint32_t kTrack = 0x2D2D34u;
constexpr uint32_t kSignalLost = 0x801010u;
constexpr uint32_t kDivider = launcher::style::kTrack;
constexpr uint32_t kOctetColors[2] = {kCyan, kWhite};

constexpr int kFillCells = 9;
constexpr int kPercentRight = 28;
constexpr int kBoltX = 30;
constexpr int kBarsX = 41;
constexpr int kDividerY = 7;
constexpr int kAddressTop = 10;
constexpr int kOctetGap = 2;

constexpr int64_t kAwayMs = 250;
constexpr float kFillMs = 320;
constexpr int64_t kBlinkMs = 500;
constexpr int64_t kShakeEveryMs = 2400;
using paint::kShake;
constexpr int64_t kBoltDropMs = 350;
constexpr float kBoltFallMs = 420;
constexpr int64_t kBurstAtMs = 120;
constexpr float kBurstMs = 300;
constexpr int64_t kBoltPulseEveryMs = 1200;
constexpr int64_t kWaveEveryMs = 1600;
constexpr float kWaveMs = 650;
constexpr int64_t kPingEveryMs = 3200;
constexpr int64_t kPingStaggerMs = 90;
constexpr float kPingMs = 260;
constexpr int64_t kBarStaggerMs = 80;
constexpr float kBarGrowMs = 300;
constexpr float kBarFallMs = 360;
constexpr int64_t kScanStepMs = 140;
constexpr int kScan[6] = {0, 1, 2, 3, 2, 1};
constexpr int64_t kRollSettleMs = 260;
constexpr int64_t kRollStaggerMs = 55;
constexpr int64_t kRollTickMs = 60;
constexpr float kLandMs = 260;
constexpr int64_t kLetterStaggerMs = 45;
constexpr float kLetterDropMs = 320;
constexpr int64_t kBreathEveryMs = 2000;

constexpr const char* kBolt[] = {".#.", "##.", "###", ".##", ".#."};

int fillCells(float pct) {
  return pct < 0.5f ? 0 : std::max(1, static_cast<int>(std::lround(pct * kFillCells / 100.0f)));
}

int drop(float fall, float t) { return -static_cast<int>(std::lround(fall * (1.0f - bounce(t)))); }

int inkWidth(const GfxFont& font, std::string_view s) { return text::measure(font, s).inkWidth(); }

// Left ink column of character i when `s` is drawn with its ink box at x = 0.
int charX(const GfxFont& font, std::string_view s, std::size_t i) {
  return inkWidth(font, s.substr(0, i + 1)) - inkWidth(font, s.substr(i, 1));
}

bool splitOctets(std::string_view ip, std::string_view (&octets)[4]) {
  std::size_t start = 0;
  for (int i = 0; i < 4; ++i) {
    const std::size_t end = i < 3 ? ip.find('.', start) : ip.size();
    if (end == std::string_view::npos || end == start || end - start > 3) return false;
    octets[i] = ip.substr(start, end - start);
    if (!std::all_of(octets[i].begin(), octets[i].end(), [](char ch) { return ch >= '0' && ch <= '9'; }))
      return false;
    start = end + 1;
  }
  return true;
}

// The bottom line shows an IPv4 address or a state; a link without a usable address still counts
// as connecting.
net::LinkPhase shownPhase(const net::LinkStatus& wifi) {
  std::string_view octets[4];
  if (wifi.phase == net::LinkPhase::Connected && splitOctets(wifi.endpoint, octets))
    return net::LinkPhase::Connected;
  if (wifi.phase == net::LinkPhase::Connected || wifi.phase == net::LinkPhase::Connecting)
    return net::LinkPhase::Connecting;
  return net::LinkPhase::Offline;
}

template <typename Draw>
void withinRows(Canvas& c, int top, int bottom, Draw draw) {
  const ClipScope clip(c, 0, top, c.width(), bottom - top + 1);
  draw();
}

}

float Tc002StatusApp::batteryValue(int64_t now) const {
  const float t = progress(now, batteryAt_, kFillMs);
  if (t >= 1.0f) return static_cast<float>(battery_);
  return batteryFrom_ + (battery_ - batteryFrom_) * easeOut(t);
}

// Compares this frame's state with the previous frame's and stamps what changed. After a gap in
// the frames it starts over from the current state.
void Tc002StatusApp::observe(const RuntimeState& rt, int64_t now) {
  const int battery = rt.hasBattery ? std::min<int>(rt.batteryPercent, 100) : -1;
  const net::LinkPhase phase = shownPhase(rt.wifi);
  const bool connected = phase == net::LinkPhase::Connected;
  const std::string& address = connected ? rt.wifi.endpoint : std::string();
  const int bars = connected ? net::signalBars(rt.wifiRssi) : 0;
  const bool away = now - lastFrameAt_ > kAwayMs;
  lastFrameAt_ = now;
  if (away) {
    battery_ = battery;
    power_ = rt.externalPower;
    bars_ = barsFrom_ = bars;
    phase_ = phase;
    address_ = address;
    batteryAt_ = powerAt_ = barsAt_ = textAt_ = kNever;
    effects_.clear();
    return;
  }
  if (battery != battery_) {
    const bool animate = battery >= 0 && battery_ >= 0;
    const float shown = batteryValue(now);
    if (animate) {
      const int before = fillCells(shown), after = fillCells(static_cast<float>(battery));
      const uint32_t fill = batteryLevelColor(static_cast<int>(std::lround(shown)), rt.lowBattery);
      if (battery > shown) effects_.add({Fx::Spark, now, after, 3, 0, batteryLevelColor(battery, rt.lowBattery)});
      else if (before > after) effects_.add({Fx::Drop, now, 1 + after, 1, before - after, fill});
    }
    batteryFrom_ = animate ? shown : static_cast<float>(battery);
    battery_ = battery;
    batteryAt_ = animate ? now : kNever;
  }
  if (rt.externalPower != power_) {
    power_ = rt.externalPower;
    powerAt_ = now;
  }
  if (bars != bars_) {
    barsFrom_ = bars_;
    bars_ = bars;
    barsAt_ = now;
  }
  if (phase != phase_ || address != address_) {
    phase_ = phase;
    address_ = address;
    textAt_ = now;
  }
}

// Shell, fill and the percentage, right-aligned so the bolt after it keeps its column.
void Tc002StatusApp::drawBattery(Canvas& c, const GfxFont& font, bool low, int64_t now) const {
  c.drawRect(0, 0, 11, 5, kShell);
  c.fillRect(11, 1, 1, 3, kShell);
  if (battery_ < 0) {
    drawInk(c, font, kPercentRight + 1 - inkWidth(font, "--%"), 0, "--%", kShell);
    return;
  }
  const float value = batteryValue(now);
  const int shown = static_cast<int>(std::lround(value));
  const uint32_t color = batteryLevelColor(shown, low);
  const int cells = fillCells(value);
  const bool moving = progress(now, batteryAt_, kFillMs) < 1.0f;
  const bool draining = low && !power_;
  const int64_t charging = powerAt_ == kNever ? now : now - powerAt_ - kBoltDropMs;
  int wave = -2;
  if (power_ && charging >= 0 && !moving) {
    const float t = progress(charging % kWaveEveryMs, 0, kWaveMs);
    if (t < 1.0f) wave = static_cast<int>(easeInOut(t) * (cells + 1)) - 1;
  }
  if (!(draining && !moving && (now / kBlinkMs) % 2 != 0)) {
    for (int i = 0; i < cells; ++i) {
      float glow = 0.0f;
      if (moving && i == cells - 1) glow = 0.8f;
      else if (i == wave) glow = 0.75f;
      else if (i == wave - 1) glow = 0.35f;
      c.fillRect(1 + i, 1, 1, 3, whiten(color, glow));
    }
  }
  const int64_t shake = now % kShakeEveryMs;
  const int dx = draining && shake < 150 ? kShake[shake / 25] : 0;
  const std::string text = formatBattery(shown);
  drawInk(c, font, kPercentRight + 1 - inkWidth(font, text) + dx, 0, text, color);
}

// Drops in with a bounce and a burst on USB power, breathes while it stays, falls away when it goes.
void Tc002StatusApp::drawBolt(Canvas& c, int64_t now) const {
  const int64_t age = now - powerAt_;
  int dy = 0;
  uint32_t color = kBoltColor;
  if (power_) {
    const int64_t landed = powerAt_ == kNever ? now : age - kBoltDropMs;
    if (landed < 0) dy = drop(6.0f, progress(age, 0, kBoltDropMs));
    else color = scale(kBoltColor, breathe(landed, kBoltPulseEveryMs, 0.44f));
  } else if (age < static_cast<int64_t>(kBoltFallMs)) {
    dy = static_cast<int>(std::lround(16.0f * easeIn(age / kBoltFallMs)));
    color = scale(kBoltColor, 1.0f - age / kBoltFallMs);
  } else {
    return;
  }
  for (int y = 0; y < 5; ++y)
    for (int x = 0; x < 3; ++x)
      if (kBolt[y][x] == '#') c.setPixel(kBoltX + x, y + dy, color);

  const float burst = progress(age, kBurstAtMs, kBurstMs);
  if (!power_ || powerAt_ == kNever || burst < 0.0f || burst > 1.0f) return;
  paint::drawBurst<paint::ParticleStyle::Status>(c,
      {Fx::Burst, powerAt_, kBoltX + 1, 2, 0, kBoltColor}, burst * kBurstMs, burst);
}

void Tc002StatusApp::drawSignal(Canvas& c, int64_t now) const {
  for (int i = 0; i < 4; ++i) {
    const int x = kBarsX + 3 * i, height = 2 + i;
    uint32_t track = kTrack;
    if (phase_ == net::LinkPhase::Offline) track = kSignalLost;
    else if (phase_ == net::LinkPhase::Connecting && kScan[(now / kScanStepMs) % 6] == i) track = kAmber;
    c.fillRect(x, 5 - height, 2, height, track);
    if (phase_ != net::LinkPhase::Connected) continue;
    if (i < bars_) {
      const int64_t start = i < barsFrom_ ? kNever : barsAt_ + kBarStaggerMs * (i - barsFrom_);
      const int grown = static_cast<int>(std::lround(height * bounce(progress(now, start, kBarGrowMs))));
      const float ping = pulse(now % kPingEveryMs, kPingStaggerMs * i, kPingMs);
      c.fillRect(x, 5 - grown, 2, grown, whiten(kCyan, 1.0f - 0.9f * ping));
    } else if (i < barsFrom_ && now - barsAt_ < static_cast<int64_t>(kBarFallMs)) {
      const float t = progress(now, barsAt_, kBarFallMs);
      const int dy = static_cast<int>(std::lround(8.0f * easeIn(t)));
      c.fillRect(x, 5 - height + dy, 2, height, scale(kWhite, 1.0f - t));
    }
  }
}

// Sparks spray down from the fill head, lost cells fall; both vanish at the divider.
void Tc002StatusApp::drawEffects(Canvas& c, int64_t now) const {
  paint::drawParticles<paint::ParticleStyle::Status>(c, effects_, now);
}

// Octets in alternating colours with a wider gap. After a change,
// digits roll and land from left to right.
void Tc002StatusApp::drawAddress(Canvas& c, const GfxFont& font, int64_t now) const {
  std::string_view octets[4];
  splitOctets(address_, octets);
  int width = 3 * kOctetGap;
  for (std::string_view octet : octets) width += inkWidth(font, octet);
  int x = (c.width() - width) / 2;
  int digit = 0;
  for (int i = 0; i < 4; ++i) {
    const uint32_t color = kOctetColors[i % 2];
    for (std::size_t j = 0; j < octets[i].size(); ++j, ++digit) {
      const int64_t settle = textAt_ + kRollSettleMs + kRollStaggerMs * digit;
      const int cx = x + charX(font, octets[i], j);
      if (now < settle) {
        const int tick = static_cast<int>((now - textAt_) / kRollTickMs);
        const char rolled = static_cast<char>('0' + static_cast<int>(noise(textAt_, digit * 131 + tick) * 10));
        drawInk(c, font, cx, kAddressTop, std::string_view(&rolled, 1), scale(color, 0.45f));
      } else {
        drawInk(c, font, cx, kAddressTop + drop(2.0f, progress(now, settle, kLandMs)), octets[i].substr(j, 1), color);
      }
    }
    x += inkWidth(font, octets[i]) + kOctetGap;
  }
}

// Letters drop in one after another; NO WIFI then breathes.
void Tc002StatusApp::drawMessage(Canvas& c, const GfxFont& font, int64_t now) const {
  const bool offline = phase_ == net::LinkPhase::Offline;
  const std::string_view text = offline ? "NO WIFI" : "CONNECTING";
  const uint32_t base = offline ? kRed : kAmber;
  const int64_t landed = textAt_ == kNever
      ? 0 : textAt_ + kLetterStaggerMs * static_cast<int64_t>(text.size()) + static_cast<int64_t>(kLetterDropMs);
  const uint32_t color = offline && now >= landed ? scale(base, breathe(now - landed, kBreathEveryMs, 0.24f)) : base;
  const int x0 = (c.width() - inkWidth(font, text)) / 2;
  for (std::size_t j = 0; j < text.size(); ++j) {
    const int64_t start = textAt_ + kLetterStaggerMs * static_cast<int64_t>(j);
    if (text[j] == ' ' || now < start) continue;
    drawInk(c, font, x0 + charX(font, text, j), kAddressTop + drop(9.0f, progress(now, start, kLetterDropMs)),
            text.substr(j, 1), color);
  }
}

void Tc002StatusApp::render(Canvas& canvas, const RenderCtx& ctx) {
  const GfxFont& font = *ctx.fonts->small().font;
  const int64_t now = ctx.nowMs;
  observe(*ctx.runtime, now);

  withinRows(canvas, 0, kDividerY - 1, [&] {
    drawBattery(canvas, font, ctx.runtime->lowBattery, now);
    drawBolt(canvas, now);
    drawSignal(canvas, now);
    drawEffects(canvas, now);
  });
  for (int x = 0; x < canvas.width(); x += 2) canvas.setPixel(x, kDividerY, kDivider);
  withinRows(canvas, kDividerY + 1, canvas.height() - 1, [&] {
    if (phase_ == net::LinkPhase::Connected) drawAddress(canvas, font, now);
    else drawMessage(canvas, font, now);
  });
}

}
