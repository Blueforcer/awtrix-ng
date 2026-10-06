#pragma once

#include <cstddef>
#include <string_view>

#include "core/render/Font.h"

namespace awtrix {

struct FontEntry {
  const char* name;
  const GfxFont* font;
  int ascent;
  int descent;
  int lineHeight;
};

// Borrowed, immutable tables: selecting a font never allocates or loads a file. Every catalog
// starts with small, the font a page uses when it names none.
struct FontCatalog {
  const FontEntry* entries = nullptr;
  std::size_t count = 0;

  const FontEntry* find(std::string_view name) const {
    for (std::size_t i = 0; i < count; ++i)
      if (name == entries[i].name) return &entries[i];
    return nullptr;
  }
  const FontEntry& small() const { return entries[0]; }
};

// The baseline of a page's text: the font's line box sits in the middle of `rows`, which puts
// small and large on row 6 of an eight-row line.
constexpr int pageBaseline(const FontEntry& f, int rows = 8) {
  return (rows - f.ascent - f.descent) / 2 + f.ascent;
}

}
