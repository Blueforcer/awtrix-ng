#pragma once

#include "core/render/Font.h"
#include "media/AwtrixFont.h"

namespace awtrix {

// Both sizes draw from the same bitmap blob and differ only in their glyph and range tables, so
// the second font costs metadata rather than another copy of the pixels.
inline const GfxFont& awtrixFont(FontId id) {
  static const GfxFont fonts[kFontCount] = {
      {AwtrixBitmaps, AwtrixGlyphsSmall, kAwtrixFontFirst, kAwtrixFontLast,
       kAwtrixYAdvanceSmall, AwtrixRangesSmall, kAwtrixRangeCountSmall},
      {AwtrixBitmaps, AwtrixGlyphsLarge, kAwtrixFontFirst, kAwtrixFontLast,
       kAwtrixYAdvanceLarge, AwtrixRangesLarge, kAwtrixRangeCountLarge},
  };
  const uint8_t i = static_cast<uint8_t>(id);
  return fonts[i < kFontCount ? i : 0];
}

inline const GfxFont& awtrixFont() { return awtrixFont(FontId::Small); }

}
