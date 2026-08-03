#include "core/render/TextEncoding.h"

namespace awtrix {
namespace text {

namespace {

constexpr uint32_t kPlaceholder = '?';

bool isContinuation(unsigned char b) { return (b & 0xC0) == 0x80; }

void skipMalformed(std::string_view in, std::size_t& i) {
  ++i;
  while (i < in.size() && isContinuation(static_cast<unsigned char>(in[i]))) ++i;
}

}

uint32_t nextCodepoint(std::string_view in, std::size_t& i) {
  const unsigned char lead = static_cast<unsigned char>(in[i]);
  if (lead < 0x80) {
    ++i;
    return lead;
  }

  int extra;
  uint32_t cp;
  uint32_t shortest;
  if ((lead & 0xE0) == 0xC0) {
    extra = 1;
    cp = lead & 0x1Fu;
    shortest = 0x80;
  } else if ((lead & 0xF0) == 0xE0) {
    extra = 2;
    cp = lead & 0x0Fu;
    shortest = 0x800;
  } else if ((lead & 0xF8) == 0xF0) {
    extra = 3;
    cp = lead & 0x07u;
    shortest = 0x10000;
  } else {
    skipMalformed(in, i);
    return kInvalidCodepoint;
  }

  if (i + static_cast<std::size_t>(extra) >= in.size()) {
    skipMalformed(in, i);
    return kInvalidCodepoint;
  }
  for (int k = 1; k <= extra; ++k) {
    const unsigned char b = static_cast<unsigned char>(in[i + static_cast<std::size_t>(k)]);
    if (!isContinuation(b)) {
      skipMalformed(in, i);
      return kInvalidCodepoint;
    }
    cp = (cp << 6) | (b & 0x3Fu);
  }
  i += static_cast<std::size_t>(extra) + 1;

  if (cp < shortest) return kInvalidCodepoint;
  if (cp >= 0xD800 && cp <= 0xDFFF) return kInvalidCodepoint;
  if (cp > 0x10FFFF) return kInvalidCodepoint;
  return cp;
}

bool isValidUtf8(const std::string& in) {
  std::size_t i = 0;
  while (i < in.size())
    if (nextCodepoint(in, i) == kInvalidCodepoint) return false;
  return true;
}

std::string fromStreamBytes(const std::string& in) {
  const bool utf8 = isValidUtf8(in);
  std::string out;
  out.reserve(in.size());
  for (char c : in) {
    const unsigned char b = static_cast<unsigned char>(c);
    if (b < 0x20 || b == 0x7F) continue;
    if (utf8 || b < 0x80) {
      out.push_back(c);
      continue;
    }
    out.push_back(static_cast<char>(0xC0 | (b >> 6)));
    out.push_back(static_cast<char>(0x80 | (b & 0x3F)));
  }
  return out;
}

const FontGlyph* glyphFor(const GfxFont& font, uint32_t cp) {
  if (cp >= font.first && cp <= font.last) return &font.glyphs[cp - font.first];
  for (uint8_t r = 0; r < font.rangeCount; ++r) {
    const FontRange& range = font.ranges[r];
    if (cp < range.first || cp > range.last) continue;
    const uint16_t slot = range.index[cp - range.first];
    return slot ? &font.glyphs[slot - 1] : nullptr;
  }
  return nullptr;
}

bool GlyphIter::next(const FontGlyph*& glyph) {
  if (i_ >= s_.size()) return false;
  const uint32_t cp = nextCodepoint(s_, i_);
  glyph = cp == kInvalidCodepoint ? nullptr : glyphFor(font_, cp);
  if (!glyph) glyph = glyphFor(font_, kPlaceholder);
  return true;
}

namespace {

// Covers the scripts the bundled fonts ship glyphs for. The Latin-Extended and Cyrillic blocks
// alternate upper/lower per codepoint, so those folds are pure parity instead of a lookup table.
uint32_t upperCodepoint(uint32_t cp) {
  if (cp >= 'a' && cp <= 'z') return cp - 0x20;
  if (cp < 0x80) return cp;

  if (cp >= 0xE0 && cp <= 0xFE && cp != 0xF7) return cp - 0x20;
  if (cp == 0xFF) return 0x178;
  if (cp == 0x131) return 'I';
  if (cp == 0x17F) return 'S';

  if (cp >= 0x100 && cp <= 0x137) return (cp & 1) ? cp - 1 : cp;
  if (cp >= 0x139 && cp <= 0x148) return (cp & 1) ? cp : cp - 1;
  if (cp >= 0x14A && cp <= 0x177) return (cp & 1) ? cp - 1 : cp;
  if (cp >= 0x179 && cp <= 0x17E) return (cp & 1) ? cp : cp - 1;

  if (cp == 0x3AC) return 0x386;
  if (cp >= 0x3AD && cp <= 0x3AF) return cp - 0x25;
  if (cp == 0x3C2) return 0x3A3;
  if (cp >= 0x3B1 && cp <= 0x3CB) return cp - 0x20;
  if (cp == 0x3CC) return 0x38C;
  if (cp >= 0x3CD && cp <= 0x3CE) return cp - 0x3F;

  if (cp >= 0x430 && cp <= 0x44F) return cp - 0x20;
  if (cp >= 0x450 && cp <= 0x45F) return cp - 0x50;
  if (cp >= 0x460 && cp <= 0x481) return (cp & 1) ? cp - 1 : cp;
  if (cp >= 0x48A && cp <= 0x4BF) return (cp & 1) ? cp - 1 : cp;
  if (cp >= 0x4C1 && cp <= 0x4CE) return (cp & 1) ? cp : cp - 1;
  if (cp >= 0x4D0 && cp <= 0x52F) return (cp & 1) ? cp - 1 : cp;
  return cp;
}

void appendUtf8(std::string& out, uint32_t cp) {
  if (cp < 0x80) {
    out.push_back(static_cast<char>(cp));
  } else if (cp < 0x800) {
    out.push_back(static_cast<char>(0xC0 | (cp >> 6)));
    out.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
  } else if (cp < 0x10000) {
    out.push_back(static_cast<char>(0xE0 | (cp >> 12)));
    out.push_back(static_cast<char>(0x80 | ((cp >> 6) & 0x3F)));
    out.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
  } else {
    out.push_back(static_cast<char>(0xF0 | (cp >> 18)));
    out.push_back(static_cast<char>(0x80 | ((cp >> 12) & 0x3F)));
    out.push_back(static_cast<char>(0x80 | ((cp >> 6) & 0x3F)));
    out.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
  }
}

}

std::string toUpperUtf8(std::string_view s) {
  std::string out;
  out.reserve(s.size());
  std::size_t i = 0;
  while (i < s.size()) {
    const std::size_t start = i;
    const uint32_t cp = nextCodepoint(s, i);
    if (cp == kInvalidCodepoint) {
      out.append(s.substr(start, i - start));
      continue;
    }
    appendUtf8(out, upperCodepoint(cp));
  }
  return out;
}

std::size_t glyphCount(const GfxFont& font, std::string_view s) {
  GlyphIter it(font, s);
  const FontGlyph* g = nullptr;
  std::size_t n = 0;
  while (it.next(g)) ++n;
  return n;
}

}
}
