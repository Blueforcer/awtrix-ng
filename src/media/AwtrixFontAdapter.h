#pragma once

#include "core/render/Font.h"
#include "media/AwtrixFont.h"
#if defined(AWTRIX_COMPACT_FONTS) && AWTRIX_COMPACT_FONTS
#include "media/MatrixFontsCompact.h"
#else
#include "media/MatrixFonts.h"
#endif

namespace awtrix {

// Both sizes draw from the same bitmap blob and differ only in their glyph and range tables, so
// the second font costs metadata rather than another copy of the pixels.
inline constexpr GfxFont kAwtrixSmall = {AwtrixBitmaps, AwtrixGlyphsSmall, kAwtrixFontFirst,
    kAwtrixFontLast, kAwtrixYAdvanceSmall, AwtrixRangesSmall, kAwtrixRangeCountSmall};
inline constexpr GfxFont kAwtrixLarge = {AwtrixBitmaps, AwtrixGlyphsLarge, kAwtrixFontFirst,
    kAwtrixFontLast, kAwtrixYAdvanceLarge, AwtrixRangesLarge, kAwtrixRangeCountLarge};

// The system font: boot, setup and status screens.
inline const GfxFont& awtrixFont() { return kAwtrixSmall; }

inline const FontCatalog& awtrixFontCatalog() {
  static constexpr FontEntry entries[] = {
      {"small", &kAwtrixSmall, 6, 1, kAwtrixYAdvanceSmall},
      {"large", &kAwtrixLarge, 6, 2, kAwtrixYAdvanceLarge},
      kMatrixFontEntries[0], kMatrixFontEntries[1],
#if !defined(AWTRIX_COMPACT_FONTS) || !AWTRIX_COMPACT_FONTS
      kMatrixFontEntries[2],
      kMatrixFontEntries[3], kMatrixFontEntries[4], kMatrixFontEntries[5],
      kMatrixFontEntries[6], kMatrixFontEntries[7], kMatrixFontEntries[8],
      kMatrixFontEntries[9],
#endif
  };
  static const FontCatalog catalog{entries, sizeof(entries) / sizeof(entries[0])};
  return catalog;
}

}
