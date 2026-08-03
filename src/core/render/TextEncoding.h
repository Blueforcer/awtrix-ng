#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>

#include "core/render/Font.h"

namespace awtrix {
namespace text {

constexpr uint32_t kInvalidCodepoint = 0xFFFFFFFFu;

// Decodes the sequence at i and advances i past it. Malformed, overlong and surrogate input
// returns kInvalidCodepoint, but i still moves on so callers cannot loop forever.
uint32_t nextCodepoint(std::string_view in, std::size_t& i);

bool isValidUtf8(const std::string& in);

// Cleans bytes arriving over serial or HTTP: drops control characters, and if the input is not
// valid UTF-8 it is assumed to be Latin-1 and re-encoded.
std::string fromStreamBytes(const std::string& in);

const FontGlyph* glyphFor(const GfxFont& font, uint32_t cp);

// Walks a string glyph by glyph, substituting '?' for any codepoint the font has no glyph for, so
// the glyph count always matches the codepoint count.
class GlyphIter {
 public:
  GlyphIter(const GfxFont& font, std::string_view s) : font_(font), s_(s) {}

  bool next(const FontGlyph*& glyph);

 private:
  const GfxFont& font_;
  std::string_view s_;
  std::size_t i_ = 0;
};

std::size_t glyphCount(const GfxFont& font, std::string_view s);

std::string toUpperUtf8(std::string_view s);

}
}
