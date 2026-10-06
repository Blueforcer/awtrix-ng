#pragma once

#include <cstdint>
#include <string_view>

#include "core/render/Canvas.h"
#include "platform/tc002/contract/terminal_glyphs.h"

namespace awtrix {
namespace render {
namespace terminal {

// The TC002 terminal look: the intro's boot log and the info screen after it.
inline constexpr uint32_t kPhosphor = 0x00FF40u;
inline constexpr uint32_t kPhosphorDim = 0x00A000u;
inline constexpr uint32_t kAmber = 0xFFA000u;

inline constexpr int kGlyphW = TC002_GLYPH_WIDTH;
inline constexpr int kGlyphH = TC002_GLYPH_HEIGHT;

// The five rows of a character in its monospace 3x5 cell, bit 2 being the left column; nullptr
// for a character the font lacks. Lower case reads as upper case.
const uint8_t* glyphRows(char ch);
void drawCell(Canvas& c, int x, int y, char ch, uint32_t rgb);

// Proportional text: every character as wide as its ink, one column apart, "1" in a narrow form,
// characters the font lacks as a blank cell. y is the top row.
int textWidth(std::string_view s);
void drawText(Canvas& c, int x, int y, std::string_view s, uint32_t rgb);

}
}
}
