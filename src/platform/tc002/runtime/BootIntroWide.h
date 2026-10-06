#pragma once

#include <cstdint>

#include "core/render/Canvas.h"
#include "core/render/Font.h"

namespace awtrix {
namespace render {

inline constexpr int64_t kBootIntroWideMs = 5000;

// Decoded frames in front of the boot sound's first sample: LAME's encoder delay (576) plus the
// MP3 decoder's own (529). Dropping them puts that sample at t = 0.
inline constexpr uint32_t kBootSoundLeadFrames = 1105;

// The power-on intro of wide panels (the TC002's 52x16). Pure function of nowMs - startMs, so
// the same instant always draws the same frame; t = 0 is the first audible sample of the boot
// sound, and from kBootIntroWideMs on the final frame holds. nowMs is never before startMs.
void drawBootIntroWide(Canvas& c, const GfxFont& font, int64_t startMs, int64_t nowMs);

}
}
