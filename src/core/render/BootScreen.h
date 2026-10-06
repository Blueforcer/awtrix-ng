#pragma once

#include <cstdint>

#include <string>

#include "core/render/Canvas.h"
#include "core/render/Font.h"

namespace awtrix {
namespace render {

inline constexpr int64_t kBootIntroMs = 2590;

// How long the info screen after the intro stays up when everything on it fits the panel.
inline constexpr int64_t kBootInfoHoldMs = 3000;

// What the info screen between the intro and the first app shows. An empty address leaves its
// line out.
struct BootInfo {
  std::string version;
  std::string address;
};

void drawBootLogo(Canvas& c, const GfxFont& font, int64_t startMs, int64_t nowMs);

// Where a line textW pixels wide starts inside an area areaW wide, elapsedMs into the info screen:
// centred when it fits, otherwise entering from the right edge and scrolling left.
int bootInfoLineX(int areaW, int textW, int64_t elapsedMs);

// Whether that line still needs the screen: a line that fits for kBootInfoHoldMs, a scrolling one
// until it has left the area on the left.
bool bootInfoLineShowing(int areaW, int textW, int64_t elapsedMs);

// The info screen of every panel size: on a single text row the address (the version when there
// is none), from two rows up the version above the address. Returns true while any line still
// needs the screen, false once the apps can take over.
bool drawBootInfo(Canvas& c, const GfxFont& font, const BootInfo& info, int64_t startMs,
                  int64_t nowMs);

}
}
