#pragma once

#include <cstdint>

#include <string>

#include "core/render/Canvas.h"
#include "core/render/Font.h"

namespace awtrix {
namespace render {

inline constexpr int64_t kBootIntroMs = 2590;

void drawBootLogo(Canvas& c, const GfxFont& font, int64_t startMs, int64_t nowMs);

// Scrolls the NG block followed by the device address across the panel. Returns true while any of
// it is still on screen, false once it has all left to the left.
bool drawBootAddress(Canvas& c, const GfxFont& font, const std::string& address, int64_t startMs,
                     int64_t nowMs);

}
}
