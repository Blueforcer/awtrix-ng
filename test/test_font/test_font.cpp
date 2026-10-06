#include <unity.h>

#include <cstdint>
#include <string>
#include <vector>

#include "core/render/TextEncoding.h"
#include "core/render/TextRenderer.h"
#include "media/AwtrixFontAdapter.h"

using namespace awtrix;

namespace {

const GfxFont& smallFont() { return *awtrixFontCatalog().find("small")->font; }
const GfxFont& largeFont() { return *awtrixFontCatalog().find("large")->font; }
const GfxFont* const kFonts[] = {&smallFont(), &largeFont()};

std::size_t glyphTableSize(const GfxFont& f) {
  std::size_t highest = static_cast<std::size_t>(f.last - f.first);
  for (uint8_t r = 0; r < f.rangeCount; ++r) {
    const FontRange& range = f.ranges[r];
    for (uint32_t cp = range.first; cp <= range.last; ++cp) {
      const uint16_t slot = range.index[cp - range.first];
      if (slot > highest + 1) highest = slot - 1;
    }
  }
  return highest + 1;
}

std::vector<uint32_t> coveredCodepoints(const GfxFont& f) {
  std::vector<uint32_t> out;
  for (uint32_t cp = f.first; cp <= f.last; ++cp) out.push_back(cp);
  for (uint8_t r = 0; r < f.rangeCount; ++r) {
    const FontRange& range = f.ranges[r];
    for (uint32_t cp = range.first; cp <= range.last; ++cp)
      if (range.index[cp - range.first]) out.push_back(cp);
  }
  return out;
}

void test_ranges_are_ordered_and_disjoint() {
  const auto& catalog = awtrixFontCatalog();
  for (std::size_t i = 0; i < catalog.count; ++i) {
    const GfxFont& f = *catalog.entries[i].font;
    TEST_ASSERT_TRUE(f.rangeCount > 0);
    for (uint8_t r = 0; r < f.rangeCount; ++r) {
      const FontRange& range = f.ranges[r];
      TEST_ASSERT_TRUE_MESSAGE(range.first <= range.last, "range is inverted");
      TEST_ASSERT_TRUE_MESSAGE(r == 0 || range.first > f.ranges[r - 1].last,
                               "ranges overlap or are unsorted");
      TEST_ASSERT_TRUE_MESSAGE(range.last < f.first || range.first > f.last,
                               "range overlaps the dense span");
      TEST_ASSERT_NOT_NULL(range.index);
    }
  }
}

const FontGlyph* linearGlyphFor(const GfxFont& f, uint32_t cp) {
  if (cp >= f.first && cp <= f.last) return &f.glyphs[cp - f.first];
  for (uint8_t r = 0; r < f.rangeCount; ++r) {
    const FontRange& range = f.ranges[r];
    if (cp < range.first || cp > range.last) continue;
    const uint16_t slot = range.index[cp - range.first];
    return slot ? &f.glyphs[slot - 1] : nullptr;
  }
  return nullptr;
}

void test_lookup_matches_a_linear_scan() {
  const auto& catalog = awtrixFontCatalog();
  for (std::size_t i = 0; i < catalog.count; ++i) {
    const GfxFont& f = *catalog.entries[i].font;
    for (uint32_t cp = 0; cp <= 0x10000; ++cp)
      if (text::glyphFor(f, cp) != linearGlyphFor(f, cp))
        TEST_FAIL_MESSAGE(catalog.entries[i].name);
  }
}

void test_every_index_entry_resolves() {
  for (const GfxFont* font : kFonts) {
    const GfxFont& f = *font;
    const std::size_t glyphs = glyphTableSize(f);
    for (uint8_t r = 0; r < f.rangeCount; ++r) {
      const FontRange& range = f.ranges[r];
      for (uint32_t cp = range.first; cp <= range.last; ++cp) {
        const uint16_t slot = range.index[cp - range.first];
        if (!slot) continue;
        TEST_ASSERT_TRUE_MESSAGE(static_cast<std::size_t>(slot - 1) < glyphs, "index points past the glyph table");
        const FontGlyph& g = f.glyphs[slot - 1];
        TEST_ASSERT_TRUE_MESSAGE(g.width == 8 || g.width == 16, "glyph rows are not one or two bytes");
        TEST_ASSERT_TRUE_MESSAGE(g.height <= 8, "glyph is taller than the panel");
      }
    }
  }
}

void test_every_font_draws_ascii_and_latin1() {
  for (const GfxFont* font : kFonts) {
    const GfxFont& f = *font;
    TEST_ASSERT_NOT_NULL(text::glyphFor(f, 'A'));
    TEST_ASSERT_NOT_NULL(text::glyphFor(f, '?'));
    TEST_ASSERT_NOT_NULL(text::glyphFor(f, 0x00B0));
    TEST_ASSERT_NOT_NULL(text::glyphFor(f, 0x00E4));
    TEST_ASSERT_EQUAL_PTR(&f.glyphs[0], text::glyphFor(f, f.first));
  }
}

void test_control_codepoints_have_no_glyph() {
  for (const GfxFont* font : kFonts) {
    const GfxFont& f = *font;
    TEST_ASSERT_NULL(text::glyphFor(f, 0x0085));
    TEST_ASSERT_NULL(text::glyphFor(f, 0x009F));
  }
}

void test_every_font_covers_the_new_blocks() {
  const uint32_t wanted[] = {0x010D, 0x0159, 0x0142, 0x017C, 0x0416, 0x044F, 0x20AC,
                             0x03A9, 0x03B1, 0x1EA5, 0x1EF9, 0x01B0, 0x0259, 0x0283,
                             0x5E74, 0x6708, 0x65E5, 0xC6D4, 0xC77C, 0x20B9, 0x2103};
  const auto& catalog = awtrixFontCatalog();
  for (std::size_t i = 0; i < catalog.count; ++i) {
    const GfxFont& f = *catalog.entries[i].font;
    for (uint32_t cp : wanted) {
      const FontGlyph* g = text::glyphFor(f, cp);
      TEST_ASSERT_NOT_NULL_MESSAGE(g, "code point has no glyph");
      TEST_ASSERT_TRUE_MESSAGE(g != text::glyphFor(f, '?'), "code point draws as ?");
    }
  }
}

void test_lookalikes_reuse_the_latin_glyph() {
  const GfxFont& f = largeFont();
  TEST_ASSERT_EQUAL_PTR(text::glyphFor(f, 'A'), text::glyphFor(f, 0x0410));
  TEST_ASSERT_EQUAL_PTR(text::glyphFor(f, 'M'), text::glyphFor(f, 0x041C));
}

void test_the_no_break_space_spaces() {
  for (const GfxFont* font : kFonts) {
    const GfxFont& f = *font;
    TEST_ASSERT_EQUAL_PTR(text::glyphFor(f, ' '), text::glyphFor(f, 0x00A0));
  }
}

void test_no_covered_letter_falls_back_to_the_placeholder() {
  const std::string letters =
      "\xC4\x8D\xC5\x99\xC5\x82\xC5\xBC\xC3\xA4"
      "\xD0\x90\xD0\xB1\xD0\xB6\xD1\x8F\xD1\x91";
  for (const GfxFont* font : kFonts) {
    const GfxFont& f = *font;
    const FontGlyph* placeholder = text::glyphFor(f, '?');
    text::GlyphIter it(f, letters);
    const FontGlyph* g = nullptr;
    while (it.next(g)) {
      TEST_ASSERT_NOT_NULL(g);
      TEST_ASSERT_TRUE_MESSAGE(g != placeholder, "a covered letter drew as ?");
    }
  }
}

void test_width_counts_glyphs_not_bytes() {
  for (const GfxFont* font : kFonts) {
    const GfxFont& f = *font;
    const int degree = text::charAdvance(f, 0x00B0);
    TEST_ASSERT_TRUE(degree > 0);
    TEST_ASSERT_EQUAL_INT(degree, text::width(f, "\xC2\xB0"));
    TEST_ASSERT_EQUAL_INT(text::charAdvance(f, '2') * 2 + degree, text::width(f, "22\xC2\xB0"));
  }
}

void test_the_two_fonts_are_different() {
  const GfxFont& small = smallFont();
  const GfxFont& large = largeFont();
  TEST_ASSERT_TRUE(large.yAdvance > small.yAdvance);
  TEST_ASSERT_TRUE(text::glyphFor(large, 'A')->height > text::glyphFor(small, 'A')->height);
}

void test_both_fonts_cover_the_same_codepoints() {
  const std::vector<uint32_t> a = coveredCodepoints(smallFont());
  const std::vector<uint32_t> b = coveredCodepoints(largeFont());
  TEST_ASSERT_EQUAL_UINT(a.size(), b.size());
  for (std::size_t i = 0; i < a.size() && i < b.size(); ++i)
    TEST_ASSERT_EQUAL_UINT32(a[i], b[i]);
}

void test_catalog_keeps_profile_fonts_and_legacy_identity() {
  const auto& catalog = awtrixFontCatalog();
#if defined(AWTRIX_COMPACT_FONTS) && AWTRIX_COMPACT_FONTS
  const char* names[] = {"small", "large", "matrix-light6", "matrix-chunky8x6"};
#else
  const char* names[] = {"small", "large", "matrix-chunky6", "matrix-chunky6x", "matrix-light6",
                        "matrix-light6x", "matrix-chunky8", "matrix-chunky8x", "matrix-chunky8x6",
                        "matrix-light8", "matrix-light8x", "matrix-light8x6"};
#endif
  for (const char* name : names) TEST_ASSERT_NOT_NULL_MESSAGE(catalog.find(name), name);
  TEST_ASSERT_EQUAL_PTR(&catalog.entries[0], &catalog.small());
  TEST_ASSERT_EQUAL_PTR(&awtrixFont(), catalog.small().font);
  for (const char* name : {"small", "large"}) {
    const auto& entry = *catalog.find(name);
    for (int rows : {8, 16, 32}) {
      const int baseline = pageBaseline(entry, rows);
      const int above = baseline - entry.ascent;
      const int below = rows - baseline - entry.descent;
      TEST_ASSERT_INT_WITHIN(1, above, below);
    }
  }
  TEST_ASSERT_NULL(catalog.find("matrix-light16"));
  for (std::size_t i = 2; i < catalog.count; ++i) {
    const auto& e = catalog.entries[i];
    TEST_ASSERT_TRUE(e.lineHeight == 6 || e.lineHeight == 8);
    TEST_ASSERT_NOT_NULL(text::glyphFor(*e.font, 0x00E4));
  }
}

#if defined(AWTRIX_COMPACT_FONTS) && AWTRIX_COMPACT_FONTS
void test_compact_removed_fonts_are_unavailable() {
  const auto& catalog = awtrixFontCatalog();
  const char* removed[] = {"matrix-chunky6", "matrix-chunky6x", "matrix-chunky8", "matrix-chunky8x",
                          "matrix-light6x", "matrix-light8", "matrix-light8x", "matrix-light8x6"};
  for (const char* name : removed) {
    TEST_ASSERT_NULL_MESSAGE(catalog.find(name), name);
    for (std::size_t i = 0; i < catalog.count; ++i)
      TEST_ASSERT_TRUE_MESSAGE(std::string(name) != catalog.entries[i].name, name);
  }
  TEST_ASSERT_EQUAL_UINT(4, catalog.count);
  TEST_ASSERT_NULL(catalog.find("matrix-chunky9"));
  TEST_ASSERT_NULL(catalog.find("matrix-light6X"));
}

#endif

std::vector<uint32_t> printableCoverage(const GfxFont& f) {
  std::vector<uint32_t> out;
  for (uint32_t cp : coveredCodepoints(f))
    if (cp >= 0x20) out.push_back(cp);
  return out;
}

void test_every_font_covers_the_same_characters() {
  const auto& catalog = awtrixFontCatalog();
  const std::vector<uint32_t> reference = printableCoverage(*catalog.entries[0].font);
  TEST_ASSERT_EQUAL_UINT(731, reference.size());
  for (std::size_t i = 1; i < catalog.count; ++i) {
    const std::vector<uint32_t> got = printableCoverage(*catalog.entries[i].font);
    TEST_ASSERT_EQUAL_UINT_MESSAGE(reference.size(), got.size(), catalog.entries[i].name);
    for (std::size_t k = 0; k < reference.size() && k < got.size(); ++k)
      TEST_ASSERT_EQUAL_UINT32_MESSAGE(reference[k], got[k], catalog.entries[i].name);
  }
}

void test_every_glyph_stays_inside_its_font_box() {
  const auto& catalog = awtrixFontCatalog();
  for (std::size_t i = 0; i < catalog.count; ++i) {
    const auto& e = catalog.entries[i];
    for (uint32_t cp : printableCoverage(*e.font)) {
      std::string one;
      text::appendUtf8(one, cp);
      const auto m = text::measureInk(*e.font, one);
      const auto horizontal = text::measure(*e.font, one);
      TEST_ASSERT_EQUAL_INT(m.advance, horizontal.advance);
      TEST_ASSERT_EQUAL_INT(m.inkLeft, horizontal.inkLeft);
      TEST_ASSERT_EQUAL_INT(m.inkRight, horizontal.inkRight);
      if (!m.hasInk()) continue;
      TEST_ASSERT_TRUE_MESSAGE(-m.inkTop <= e.ascent, e.name);
      TEST_ASSERT_TRUE_MESSAGE(m.inkBottom < e.descent, e.name);
    }
  }
}

void test_matrix_fonts_share_one_bitmap() {
  const auto& catalog = awtrixFontCatalog();
  const auto* bitmap = catalog.find("matrix-light6")->font->bitmap;
  for (std::size_t i = 2; i < catalog.count; ++i)
    TEST_ASSERT_EQUAL_PTR_MESSAGE(bitmap, catalog.entries[i].font->bitmap, catalog.entries[i].name);
#if !defined(AWTRIX_COMPACT_FONTS) || !AWTRIX_COMPACT_FONTS
  const FontGlyph* chunky = text::glyphFor(*catalog.find("matrix-chunky6")->font, 0x5E74);
  const FontGlyph* light = text::glyphFor(*catalog.find("matrix-light6")->font, 0x5E74);
  TEST_ASSERT_EQUAL_UINT16(chunky->bitmapOffset, light->bitmapOffset);
#endif
}

void test_vertical_metrics_include_accents_and_ignore_blank_padding() {
#if defined(AWTRIX_COMPACT_FONTS) && AWTRIX_COMPACT_FONTS
  const auto* entry = awtrixFontCatalog().find("matrix-light6");
#else
  const auto* entry = awtrixFontCatalog().find("matrix-light8x");
#endif
  TEST_ASSERT_NOT_NULL(entry);
  const auto& f = *entry->font;
  const auto m = text::measureInk(f, "A\xC3\xA4g");
#if defined(AWTRIX_COMPACT_FONTS) && AWTRIX_COMPACT_FONTS
  TEST_ASSERT_EQUAL_INT(-6, m.inkTop);
  TEST_ASSERT_EQUAL_INT(6, m.inkHeight());
#else
  TEST_ASSERT_EQUAL_INT(-8, m.inkTop);
  TEST_ASSERT_EQUAL_INT(8, m.inkHeight());
#endif
  TEST_ASSERT_EQUAL_INT(-1, m.inkBottom);
  TEST_ASSERT_EQUAL_INT(0, text::measureInk(f, "  ").inkHeight());
  const uint8_t bits[] = {0x40}; // three rows: blank, lit, blank
  const FontGlyph glyphs[] = {{0,1,3,1,0,-2}};
  const GfxFont padded{bits,glyphs,'A','A',3};
  const auto one = text::measureInk(padded,"A");
  TEST_ASSERT_EQUAL_INT(-1, one.inkTop);
  TEST_ASSERT_EQUAL_INT(-1, one.inkBottom);
}

}

void setUp() {}
void tearDown() {}

void test_a_row_map_stretches_and_splits_a_glyph() {
  const GfxFont& f = smallFont();
  const FontGlyph* g = text::glyphFor(f, '8');
  Canvas plain(8, 8);
  text::drawGlyph(plain, f, 0, -g->yOffset, g, 0xFFFFFFu);
  const uint8_t doubled[10] = {0, 0, 1, 1, 2, 2, 3, 3, 4, 4};
  Canvas big(16, 10);
  text::drawGlyphRows(big, f, 0, 0, g, 2, g->yOffset, doubled, 10, 0xFFFFFFu);
  for (int y = 0; y < 10; ++y)
    for (int x = 0; x < 16; ++x) TEST_ASSERT_EQUAL_HEX32(plain.getPixel(x / 2, y / 2), big.getPixel(x, y));
  const uint8_t split[3] = {1, 0xFF, 1};
  Canvas gap(8, 3);
  text::drawGlyphRows(gap, f, 0, 0, g, 1, g->yOffset, split, 3, 0xFFFFFFu);
  for (int x = 0; x < 8; ++x) {
    TEST_ASSERT_EQUAL_HEX32(plain.getPixel(x, 1), gap.getPixel(x, 0));
    TEST_ASSERT_EQUAL_HEX32(0u, gap.getPixel(x, 1));
    TEST_ASSERT_EQUAL_HEX32(plain.getPixel(x, 1), gap.getPixel(x, 2));
  }
}

int main(int, char**) {
  UNITY_BEGIN();
  RUN_TEST(test_a_row_map_stretches_and_splits_a_glyph);
  RUN_TEST(test_ranges_are_ordered_and_disjoint);
  RUN_TEST(test_lookup_matches_a_linear_scan);
  RUN_TEST(test_every_index_entry_resolves);
  RUN_TEST(test_every_font_draws_ascii_and_latin1);
  RUN_TEST(test_control_codepoints_have_no_glyph);
  RUN_TEST(test_every_font_covers_the_new_blocks);
  RUN_TEST(test_lookalikes_reuse_the_latin_glyph);
  RUN_TEST(test_the_no_break_space_spaces);
  RUN_TEST(test_no_covered_letter_falls_back_to_the_placeholder);
  RUN_TEST(test_width_counts_glyphs_not_bytes);
  RUN_TEST(test_the_two_fonts_are_different);
  RUN_TEST(test_both_fonts_cover_the_same_codepoints);
  RUN_TEST(test_catalog_keeps_profile_fonts_and_legacy_identity);
#if defined(AWTRIX_COMPACT_FONTS) && AWTRIX_COMPACT_FONTS
  RUN_TEST(test_compact_removed_fonts_are_unavailable);
#endif
  RUN_TEST(test_every_font_covers_the_same_characters);
  RUN_TEST(test_every_glyph_stays_inside_its_font_box);
  RUN_TEST(test_matrix_fonts_share_one_bitmap);
  RUN_TEST(test_vertical_metrics_include_accents_and_ignore_blank_padding);
  return UNITY_END();
}
