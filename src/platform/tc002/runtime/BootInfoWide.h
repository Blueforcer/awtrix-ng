#pragma once

#include <cstdint>

#include "core/render/BootScreen.h"
#include "core/render/Canvas.h"

namespace awtrix {
namespace render {

// The info screen of wide panels (the TC002's 52x16) in the intro's terminal look: "VER" and the
// firmware version on the top row, the address centred below it; without an address the version
// row alone, vertically centred. Timed like drawBootInfo: a line that fits holds for
// kBootInfoHoldMs, a longer one scrolls through once. Returns true while any line still needs the
// screen.
bool drawBootInfoWide(Canvas& c, const BootInfo& info, int64_t startMs, int64_t nowMs);

}
}
