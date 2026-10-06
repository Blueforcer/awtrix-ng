#include "platform/tc002/runtime/BootIntroWide.h"
#include "platform/tc002/contract/tc002_layout.h"

#include <cmath>
#include <cstdint>
#include <initializer_list>

#include "core/render/Color.h"
#include "core/render/Motion.h"
#include "platform/tc002/runtime/Tc002Paint.h"
#include "platform/tc002/runtime/TerminalFont.h"

namespace awtrix {
namespace render {

namespace {

constexpr int kDesignW = TC002_PANEL_WIDTH;
constexpr int kDesignH = TC002_PANEL_HEIGHT;

// Every cue below is in ms of the boot sound, counted from its first audible sample.
constexpr float kRiserStep[7] = {0, 78, 124, 175, 225, 255, 314};
constexpr float kRiserHit = 325;
constexpr float kStutter = 440;
constexpr float kStutterEnd = 513;
constexpr float kStab[3] = {605, 883, 1165};
constexpr float kStabRetrig[2] = {1330, 1355};
constexpr float kGap = 1394;
constexpr float kDrop = 1445;
constexpr float kChunk[7] = {1445, 1627, 1720, 1910, 2001, 2188, 2277};
constexpr float kAccent[2] = {1910, 2188};
constexpr float kBreak = 2549;
constexpr float kType[6] = {2549, 2644, 2770, 2842, 2912, 2978};
constexpr float kSayN = 3105;
constexpr float kSayGBuild = 3320;
constexpr float kSayG = 3388;
constexpr float kShimmer[3] = {3669, 3762, 3855};
constexpr float kShine = 3948;
constexpr float kRelease = 4209;

constexpr float kBeatMs = 278.48f;
constexpr float kFrameMs = 25.0f;

using terminal::kAmber;
using terminal::kPhosphor;
using terminal::kPhosphorDim;

constexpr uint32_t kWhite = 0xFFFFFFu;
constexpr uint32_t kCursor = 0x00FF40u;
constexpr uint32_t kBadgeFill = 0x009000u;
constexpr uint32_t kTagGlow = 0x00FF00u;

using motion::clamp01;
using motion::pulse;

bool within(float a, float from, float to) { return a >= from && a < to; }

using paint::flash;
using paint::flashLevel;

// A terminal cursor: solid for its first half period after `since`, then blinking.
bool cursorOn(float a, float since, float halfMs) {
  if (a < since) return false;
  return static_cast<int>((a - since) / halfMs) % 2 == 0;
}

struct Pen {
  Canvas& c;
  int ox;
  int oy;

  void set(int x, int y, uint32_t rgb) const { c.setPixel(ox + x, oy + y, rgb); }
  uint32_t get(int x, int y) const { return c.getPixel(ox + x, oy + y); }
  void fill(int x, int y, int w, int h, uint32_t rgb) const {
    for (int j = 0; j < h; ++j)
      for (int i = 0; i < w; ++i) set(x + i, y + j, rgb);
  }
};

template <typename Fn>
void forEachGlyphPixel(const uint8_t* rows, int w, int h, int x, int y, Fn plot) {
  for (int r = 0; r < h; ++r)
    for (int col = 0; col < w; ++col)
      if (rows[r] & (1u << (w - 1 - col))) plot(x + col, y + r);
}

// ---------------------------------------------------------------- 3x5 terminal font, 4 px pitch

constexpr int kCols = 13;
int colX(int col) { return col * 4; }

void drawTinyChar(const Pen& p, int col, int y, char ch, uint32_t rgb) {
  terminal::drawCell(p.c, p.ox + colX(col), p.oy + y, ch, rgb);
}

int drawTiny(const Pen& p, int col, int y, const char* s, uint32_t rgb) {
  for (; *s && col < kCols; ++s, ++col) drawTinyChar(p, col, y, *s, rgb);
  return col;
}

int textLen(const char* s) {
  int n = 0;
  while (s[n]) ++n;
  return n;
}

void drawTinyCursor(const Pen& p, int col, int y, uint32_t rgb) { p.fill(colX(col), y, 3, 5, rgb); }

// A status tag in the last two columns: a rounded green box around "OK", on the newest line only.
void drawTag(const Pen& p, int y, uint32_t fill, uint32_t ink) {
  const int left = colX(11) - 1;
  const int right = kDesignW - 1;
  for (int yy = y - 1; yy <= y + 5; ++yy)
    for (int x = left; x <= right; ++x) {
      const bool corner = (x == left || x == right) && (yy == y - 1 || yy == y + 5);
      if (!corner) p.set(x, yy, fill);
    }
  drawTinyChar(p, 11, y, 'O', ink);
  drawTinyChar(p, 12, y, 'K', ink);
}

// A whole text row in inverse video, the way a terminal highlights the line that just changed.
void drawInverseRow(const Pen& p, int y, const char* s, bool tagged, uint32_t fill) {
  p.fill(0, y - 1, kDesignW, 7, fill);
  drawTiny(p, 0, y, s, color::kBlack);
  if (tagged) {
    drawTinyChar(p, 11, y, 'O', color::kBlack);
    drawTinyChar(p, 12, y, 'K', color::kBlack);
  }
}

// ---------------------------------------------------------------- 5x7 logo font

struct Glyph {
  int w;
  uint8_t rows[7];
};

constexpr Glyph kWord[6] = {
    {5, {0b01110, 0b10001, 0b10001, 0b11111, 0b10001, 0b10001, 0b10001}},
    {5, {0b10001, 0b10001, 0b10001, 0b10101, 0b10101, 0b11011, 0b10001}},
    {5, {0b11111, 0b00100, 0b00100, 0b00100, 0b00100, 0b00100, 0b00100}},
    {5, {0b11110, 0b10001, 0b10001, 0b11110, 0b10100, 0b10010, 0b10001}},
    {3, {0b111, 0b010, 0b010, 0b010, 0b010, 0b010, 0b111}},
    {5, {0b10001, 0b10001, 0b01010, 0b00100, 0b01010, 0b10001, 0b10001}},
};
constexpr int kWordX[6] = {1, 7, 13, 19, 25, 29};
constexpr int kWordY = 4;

constexpr uint8_t kBigN[7] = {0b10001, 0b10001, 0b11001, 0b10101, 0b10011, 0b10001, 0b10001};
constexpr uint8_t kBigG[7] = {0b01110, 0b10001, 0b10000, 0b10111, 0b10001, 0b10001, 0b01111};

constexpr int kBadgeLeft = 36;
constexpr int kBadgeHalf = 44;
constexpr int kBadgeRight = 50;
constexpr int kBadgeTop = 2;
constexpr int kBadgeBottom = 12;
constexpr int kBadgeNX = 38;
constexpr int kBadgeGX = 44;

void drawWordLetter(const Pen& p, int i, uint32_t rgb) {
  forEachGlyphPixel(kWord[i].rows, kWord[i].w, 7, kWordX[i], kWordY,
                    [&](int x, int y) { p.set(x, y, rgb); });
}

void drawBadgeBox(const Pen& p, int right, uint32_t fill) {
  for (int y = kBadgeTop; y <= kBadgeBottom; ++y)
    for (int x = kBadgeLeft; x <= right; ++x) {
      const bool corner = (x == kBadgeLeft || x == right) && (y == kBadgeTop || y == kBadgeBottom);
      if (!corner) p.set(x, y, fill);
    }
}

void drawBadgeLetters(const Pen& p, bool withG, uint32_t ink) {
  forEachGlyphPixel(kBigN, 5, 7, kBadgeNX, kWordY, [&](int x, int y) { p.set(x, y, ink); });
  if (withG) forEachGlyphPixel(kBigG, 5, 7, kBadgeGX, kWordY, [&](int x, int y) { p.set(x, y, ink); });
}

// Settled badge: phosphor brackets, white letters.
void drawStatusBrackets(const Pen& p, int right, uint32_t rgb) {
  for (int y = kBadgeTop; y <= kBadgeBottom; ++y) {
    p.set(kBadgeLeft, y, rgb);
    p.set(right, y, rgb);
  }
  for (int y : {kBadgeTop, kBadgeBottom}) {
    p.set(kBadgeLeft + 1, y, rgb);
    p.set(right - 1, y, rgb);
  }
}

// ---------------------------------------------------------------- act 1: the boot log

// The log fills the top row first, then scrolls: the newest line always sits on the bottom row.
constexpr int kRowY[2] = {1, 9};

struct LogLine {
  float at;
  const char* text;
  float tagAt;  // negative: no status tag
};

constexpr LogLine kLog[] = {
    {0, "", kRiserHit},                // RAM counter, built live below
    {kStutter, "LED 52X16", -1},       // the stutter spews three lines in three frames
    {kStutter + 25, "TIME 12:00", -1},
    {kStutter + 50, "WIFI UP", -1},
    {kStab[0], "CORE", kStab[0]},
    {kStab[1], "DISPLAY", kStab[1]},
    {kStab[2], "AUDIO", kStab[2]},
    {kStabRetrig[0], "> BO", -1},      // the prompt types its command on the stab stutter
};
constexpr int kLogCount = sizeof(kLog) / sizeof(kLog[0]);

// Memory counter of the power-on self test, climbing with the riser.
int ramKb(float a) {
  static constexpr float kAt[8] = {0, 78, 124, 175, 225, 255, 314, 325};
  static constexpr float kKb[8] = {0, 64, 128, 256, 384, 512, 576, 640};
  if (a >= kAt[7]) return 640;
  int i = 0;
  while (i < 6 && a >= kAt[i + 1]) ++i;
  const float k = (a - kAt[i]) / (kAt[i + 1] - kAt[i]);
  const float kb = kKb[i] + (kKb[i + 1] - kKb[i]) * k;
  return static_cast<int>(kb / 8.0f) * 8;
}

void formatRam(char* out, int kb) {
  out[0] = 'R';
  out[1] = 'A';
  out[2] = 'M';
  out[3] = ' ';
  out[4] = static_cast<char>('0' + kb / 100);
  out[5] = static_cast<char>('0' + (kb / 10) % 10);
  out[6] = static_cast<char>('0' + kb % 10);
  out[7] = 'K';
  out[8] = 0;
}

void lineText(int i, float a, char* buf) {
  if (i == 0) {
    formatRam(buf, ramKb(a));
    return;
  }
  const char* s = kLog[i].text;
  if (i == kLogCount - 1 && a >= kStabRetrig[1]) s = "> BOOT";
  int n = 0;
  for (; s[n]; ++n) buf[n] = s[n];
  buf[n] = 0;
}

void drawLogLine(const Pen& p, int i, int y, bool newest, float a) {
  char buf[kCols + 1];
  lineText(i, a, buf);
  const LogLine& line = kLog[i];
  const bool tagged = line.tagAt >= 0.0f && a >= line.tagAt;
  const float hitAge = tagged ? a - line.tagAt : 1e9f;

  if (newest && tagged && hitAge < 2 * kFrameMs) {
    drawInverseRow(p, y, buf, true, hitAge < kFrameMs ? kWhite : kPhosphor);
    return;
  }
  const float glow = newest && tagged ? pulse(a, line.tagAt + 2 * kFrameMs, 160.0f) : 0.0f;
  drawTiny(p, 0, y, buf, flash(newest ? kPhosphor : kPhosphorDim, glow));
  if (i == 0 && a < kRiserHit) {
    // The counter's digits flash on each riser note.
    float tick = 0.0f;
    for (float s : kRiserStep) tick = std::fmax(tick, pulse(a, s, 40.0f));
    drawTiny(p, 4, y, buf + 4, flash(kAmber, tick));
  }
  if (tagged) {
    if (newest) {
      drawTag(p, y, color::lerp(kBadgeFill, kTagGlow, 0.6f * glow), kWhite);
    } else {
      drawTinyChar(p, 11, y, 'O', kPhosphorDim);
      drawTinyChar(p, 12, y, 'K', kPhosphorDim);
    }
  }
}

// The memory map under the counter: one tick per text column, filling as the count climbs.
void drawMemoryTicks(const Pen& p, float a) {
  if (a >= kStutter) return;
  const int filled = a >= kRiserHit ? kCols : (ramKb(a) * kCols) / 640;
  const float hit = pulse(a, kRiserHit, 110.0f);
  for (int col = 0; col < filled; ++col) {
    const bool head = a < kRiserHit && col == filled - 1;
    const uint32_t rgb = head ? kWhite : flash(kPhosphorDim, hit);
    p.fill(colX(col), kRowY[1] + 2, 3, 1, rgb);
  }
}

void drawBootLog(const Pen& p, float a) {
  if (a < kRiserStep[1]) {
    // Power on: nothing but the cursor.
    drawTinyCursor(p, 0, kRowY[0], kCursor);
    return;
  }
  int printed = 0;
  while (printed < kLogCount && a >= kLog[printed].at) ++printed;
  const int newest = printed - 1;
  if (printed == 1) {
    drawLogLine(p, 0, kRowY[0], true, a);
    drawMemoryTicks(p, a);
  } else {
    drawLogLine(p, newest - 1, kRowY[0], false, a);
    drawLogLine(p, newest, kRowY[1], true, a);
  }

  // The cursor waits at the end of the newest line through the silence and on the prompt.
  char buf[kCols + 1];
  lineText(newest, a, buf);
  const int row = printed == 1 ? kRowY[0] : kRowY[1];
  const int col = textLen(buf) + (newest == kLogCount - 1 ? 0 : 1);
  if (within(a, kStutterEnd, kStab[0]) && cursorOn(a, kStutterEnd, 60.0f))
    drawTinyCursor(p, col, row, kCursor);
  if (newest == kLogCount - 1) drawTinyCursor(p, col, row, kCursor);
  if (a < kRiserHit && a >= kRiserStep[1]) drawTinyCursor(p, 9, kRowY[0], kCursor);
}

// ---------------------------------------------------------------- act 2: the loading bar

constexpr int kBarTop = 7;
constexpr int kBarBottom = 14;
constexpr int kChunkW = 6;
constexpr int kChunkPitch = 7;
constexpr int kChunkX0 = 2;
constexpr int kChunkTop = 9;
constexpr int kChunkH = 4;

void drawPercent(const Pen& p, int y, int pct, uint32_t rgb) {
  char buf[5];
  int n = 0;
  if (pct >= 100) buf[n++] = '1';
  if (pct >= 10) buf[n++] = static_cast<char>('0' + (pct / 10) % 10);
  buf[n++] = static_cast<char>('0' + pct % 10);
  buf[n++] = '%';
  buf[n] = 0;
  drawTiny(p, kCols - n, y, buf, rgb);
}

int chunkPct(int n) { return n >= 7 ? 100 : (n * 100 + 3) / 7; }

void drawLoading(const Pen& panel, float a) {
  // The drop blows the whole panel white for one frame before the loading screen appears.
  if (a < kDrop + kFrameMs) {
    panel.fill(0, 0, kDesignW, kDesignH, kWhite);
    return;
  }
  // On each kick accent the whole layout jolts one pixel down for a frame.
  bool jolt = false;
  for (float c : kAccent) jolt = jolt || within(a, c, c + kFrameMs);
  const Pen p{panel.c, panel.ox, panel.oy + (jolt ? 1 : 0)};

  int lit = 0;
  while (lit < 7 && a >= kChunk[lit]) ++lit;
  const bool done = lit == 7;
  // The percentage rolls up to each new block over three frames.
  const float roll = clamp01((a - kChunk[lit - 1] + kFrameMs) / (3.0f * kFrameMs));
  const int pct = chunkPct(lit - 1) +
                  static_cast<int>(std::lround((chunkPct(lit) - chunkPct(lit - 1)) * roll));

  const float drop = pulse(a, kDrop, 180.0f);
  float accent = 0.0f;
  float accentText = 0.0f;
  for (float c : kAccent) {
    accent = std::fmax(accent, pulse(a, c, 140.0f));
    accentText = std::fmax(accentText, pulse(a, c, 120.0f));
  }
  const float full = pulse(a, kChunk[6], 220.0f);

  const uint32_t label = flash(kPhosphor, std::fmax(std::fmax(drop, full), accentText));
  drawTiny(p, 0, kRowY[0], done ? "READY." : "LOADING", label);
  if (done && cursorOn(a, kChunk[6], kBeatMs / 2.0f)) drawTinyCursor(p, 6, kRowY[0], kCursor);
  if (!done) {
    static constexpr char kSpin[4] = {'|', '/', '-', '\\'};
    const int spin = static_cast<int>((a - kDrop) / (kBeatMs / 4.0f)) % 4;
    drawTinyChar(p, 8, kRowY[0], kSpin[spin], kWhite);
  }
  const float pctFlash = std::fmax(std::fmax(pulse(a, kChunk[lit - 1], 90.0f), full), accentText);
  drawPercent(p, kRowY[0], pct, flash(kAmber, pctFlash));

  const bool segmentTest = a < kDrop + 2 * kFrameMs;
  const uint32_t frame =
      segmentTest ? kWhite : flash(kPhosphorDim, std::fmax(std::fmax(drop, accent), full));
  for (int x = 0; x < kDesignW; ++x) {
    const bool corner = x == 0 || x == kDesignW - 1;
    if (!corner) {
      p.set(x, kBarTop, frame);
      p.set(x, kBarBottom, frame);
    }
  }
  for (int y = kBarTop + 1; y < kBarBottom; ++y) {
    p.set(0, y, frame);
    p.set(kDesignW - 1, y, frame);
  }

  // After the white flash the bar runs a segment test: every block lit in a white frame, then the
  // real fill.
  if (segmentTest) {
    for (int i = 0; i < 7; ++i) p.fill(kChunkX0 + i * kChunkPitch, kChunkTop, kChunkW, kChunkH, kPhosphor);
    return;
  }
  for (int i = 0; i < lit; ++i) {
    const float fresh = pulse(a, kChunk[i], 150.0f);
    const float k = std::fmax(fresh, std::fmax(0.7f * accent, full));
    p.fill(kChunkX0 + i * kChunkPitch, kChunkTop, kChunkW, kChunkH, flash(kPhosphor, k));
  }
}

// ---------------------------------------------------------------- act 3: typing the name

void drawBadge(const Pen& p, float a) {
  if (a < kSayN) return;
  int right = kBadgeHalf;
  if (a >= kSayG) {
    right = kBadgeRight;
  } else if (a >= kSayGBuild) {
    const float k = (a - kSayGBuild) / (kSayG - kSayGBuild);
    right = kBadgeHalf + 2 * static_cast<int>(1.0f + 2.0f * k);
  }
  // It lands in inverse video, N cut out of a white box, then of a green one, and then settles into
  // the status brackets. The shimmer and the shine flash the brackets towards white.
  if (a < kSayN + 2 * kFrameMs) {
    drawBadgeBox(p, right, a < kSayN + kFrameMs ? kWhite : kPhosphor);
    drawBadgeLetters(p, false, color::kBlack);
    return;
  }
  float glow = 0.4f * pulse(a, kSayG + 2 * kFrameMs, 220.0f);
  for (float s : kShimmer) glow = std::fmax(glow, 0.6f * pulse(a, s, 110.0f));
  glow = std::fmax(glow, 0.9f * pulse(a, kShine, 170.0f));
  drawStatusBrackets(p, right, flash(kPhosphor, glow));
  drawBadgeLetters(p, a >= kSayG, kWhite);
}

// A key prints in inverse video for two frames, white cell then green cell with the glyph cut out.
void drawTypedLetter(const Pen& p, int i, float a) {
  const float age = a - kType[i];
  if (age >= 2 * kFrameMs) {
    drawWordLetter(p, i, kWhite);
    return;
  }
  p.fill(kWordX[i] - 1, kWordY - 1, kWord[i].w + 2, 9, age < kFrameMs ? kWhite : kPhosphor);
  drawWordLetter(p, i, color::kBlack);
}

void drawPrompt(const Pen& p, float a) {
  int typed = 0;
  while (typed < 6 && a >= kType[typed]) ++typed;
  for (int i = 0; i < typed; ++i) drawTypedLetter(p, i, a);
  if (a < kSayN) {
    // An underscore cursor waits under the next key, and under the N once the word is typed.
    const int x = typed < 6 ? kWordX[typed] : kBadgeNX;
    p.fill(x, kWordY + 8, 5, 1, kCursor);
  }
  drawBadge(p, a);
}

// Visual bell: the whole panel flips to inverse video for two frames on the final hit.
void visualBell(const Pen& p, float a) {
  if (!within(a, kSayG, kSayG + 2 * kFrameMs)) return;
  const uint32_t on = a < kSayG + kFrameMs ? kWhite : kPhosphor;
  for (int y = 0; y < kDesignH; ++y)
    for (int x = 0; x < kDesignW; ++x) p.set(x, y, p.get(x, y) ? color::kBlack : on);
}

void drawFinalLogo(const Pen& p) {
  for (int i = 0; i < 6; ++i) drawWordLetter(p, i, kWhite);
  drawStatusBrackets(p, kBadgeRight, kPhosphor);
  drawBadgeLetters(p, true, kWhite);
}

}

// Three acts on the boot sound's cues: boot log, loading bar, typed name.
void drawBootIntroWide(Canvas& c, const GfxFont& font, int64_t startMs, int64_t nowMs) {
  static_cast<void>(font);
  c.clear(color::kBlack);
  const int64_t t = nowMs > startMs ? nowMs - startMs : 0;
  const float a = static_cast<float>(t);
  const int ox = (c.width() - kDesignW) / 2;
  const int oy = (c.height() - kDesignH) / 2;
  const Pen p{c, ox, oy};

  const ClipScope clip(c, ox, oy, kDesignW, kDesignH);
  if (a < kGap) {
    drawBootLog(p, a);
  } else if (a < kDrop) {
    if (cursorOn(a, kGap, 60.0f)) drawTinyCursor(p, 0, kRowY[0], kCursor);
  } else if (a < kBreak) {
    drawLoading(p, a);
  } else if (a < kRelease) {
    drawPrompt(p, a);
    visualBell(p, a);
  } else {
    drawFinalLogo(p);
  }
}

}

}
