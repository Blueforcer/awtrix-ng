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

// Count/clip UTF-8 by sequence starts. Validation, when required, remains a separate step.
inline std::size_t codepoints(std::string_view in) {
  std::size_t count = 0;
  for (unsigned char c : in) if ((c & 0xc0) != 0x80) ++count;
  return count;
}
inline std::string_view clipCodepoints(std::string_view in, std::size_t count) {
  for (std::size_t i = 0, seen = 0; i < in.size(); ++i) {
    if ((static_cast<unsigned char>(in[i]) & 0xc0) == 0x80) continue;
    if (seen++ == count) return in.substr(0, i);
  }
  return in;
}

// At most bytes, dropping a partial final UTF-8 sequence (including an already truncated input).
inline std::string_view clipBytes(std::string_view in, std::size_t bytes) {
  if (bytes < in.size()) in = in.substr(0, bytes);
  std::size_t start = in.size();
  unsigned tail = 0;
  while (start > 0 && tail < 4 && (static_cast<unsigned char>(in[start - 1]) & 0xc0) == 0x80) {
    --start;
    ++tail;
  }
  if (start > 0) {
    const unsigned char lead = static_cast<unsigned char>(in[start - 1]);
    const unsigned need = lead >= 0xf0 ? 3 : lead >= 0xe0 ? 2 : lead >= 0xc0 ? 1 : 0;
    if (need > tail) in = in.substr(0, start - 1);
  }
  return in;
}

bool isValidUtf8(const std::string& in);

// Cleans bytes arriving over serial or HTTP: drops control characters, and if the input is not
// valid UTF-8 it is assumed to be Latin-1 and re-encoded.
std::string fromStreamBytes(const std::string& in);

const FontGlyph* glyphFor(const GfxFont& font, uint32_t cp);

// Walks a string glyph by glyph, substituting '?' for any codepoint the font has no glyph for, so
// the glyph count always matches the codepoint count. upper shows each codepoint as its capital,
// exactly as toUpperUtf8 would.
class GlyphIter {
 public:
  GlyphIter(const GfxFont& font, std::string_view s, bool upper = false)
      : font_(font), s_(s), upper_(upper) {}

  bool next(const FontGlyph*& glyph);
  // Byte offset of the glyph the next call to next() returns.
  std::size_t offset() const { return i_; }

 private:
  const GfxFont& font_;
  std::string_view s_;
  std::size_t i_ = 0;
  bool upper_ = false;
};

std::size_t glyphCount(const GfxFont& font, std::string_view s);

void appendUtf8(std::string& out, uint32_t cp);

std::string toUpperUtf8(std::string_view s);

}
}
