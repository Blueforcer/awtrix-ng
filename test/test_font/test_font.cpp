#include <unity.h>

#include <cstdint>
#include <string>
#include <vector>

#include "core/render/TextEncoding.h"
#include "core/render/TextRenderer.h"
#include "media/AwtrixFontAdapter.h"

using namespace awtrix;

namespace {

const FontId kIds[] = {FontId::Small, FontId::Large};

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
  for (FontId id : kIds) {
    const GfxFont& f = awtrixFont(id);
    TEST_ASSERT_TRUE(f.rangeCount > 0);
    uint32_t previousEnd = f.last;
    for (uint8_t r = 0; r < f.rangeCount; ++r) {
      const FontRange& range = f.ranges[r];
      TEST_ASSERT_TRUE_MESSAGE(range.first <= range.last, "range is inverted");
      TEST_ASSERT_TRUE_MESSAGE(range.first > previousEnd, "ranges overlap or are unsorted");
      TEST_ASSERT_NOT_NULL(range.index);
      previousEnd = range.last;
    }
  }
}

void test_every_index_entry_resolves() {
  for (FontId id : kIds) {
    const GfxFont& f = awtrixFont(id);
    const std::size_t glyphs = glyphTableSize(f);
    for (uint8_t r = 0; r < f.rangeCount; ++r) {
      const FontRange& range = f.ranges[r];
      for (uint32_t cp = range.first; cp <= range.last; ++cp) {
        const uint16_t slot = range.index[cp - range.first];
        if (!slot) continue;
        TEST_ASSERT_TRUE_MESSAGE(slot - 1 < glyphs, "index points past the glyph table");
        const FontGlyph& g = f.glyphs[slot - 1];
        TEST_ASSERT_TRUE_MESSAGE(g.width <= 8, "glyph is wider than one byte per row");
        TEST_ASSERT_TRUE_MESSAGE(g.height <= 8, "glyph is taller than the panel");
      }
    }
  }
}

void test_every_font_draws_ascii_and_latin1() {
  for (FontId id : kIds) {
    const GfxFont& f = awtrixFont(id);
    TEST_ASSERT_NOT_NULL(text::glyphFor(f, 'A'));
    TEST_ASSERT_NOT_NULL(text::glyphFor(f, '?'));
    TEST_ASSERT_NOT_NULL(text::glyphFor(f, 0x00B0));
    TEST_ASSERT_NOT_NULL(text::glyphFor(f, 0x00E4));
    TEST_ASSERT_EQUAL_PTR(&f.glyphs[0], text::glyphFor(f, f.first));
  }
}

void test_control_codepoints_have_no_glyph() {
  for (FontId id : kIds) {
    const GfxFont& f = awtrixFont(id);
    TEST_ASSERT_NULL(text::glyphFor(f, 0x0085));
    TEST_ASSERT_NULL(text::glyphFor(f, 0x009F));
  }
}

void test_every_font_covers_the_new_blocks() {
  const uint32_t wanted[] = {0x010D, 0x0159, 0x0142, 0x017C, 0x0416, 0x044F, 0x20AC};
  for (FontId id : kIds) {
    const GfxFont& f = awtrixFont(id);
    for (uint32_t cp : wanted) {
      const FontGlyph* g = text::glyphFor(f, cp);
      TEST_ASSERT_NOT_NULL_MESSAGE(g, "code point has no glyph");
      TEST_ASSERT_TRUE_MESSAGE(g != text::glyphFor(f, '?'), "code point draws as ?");
    }
  }
}

void test_lookalikes_reuse_the_latin_glyph() {
  const GfxFont& f = awtrixFont(FontId::Large);
  TEST_ASSERT_EQUAL_PTR(text::glyphFor(f, 'A'), text::glyphFor(f, 0x0410));
  TEST_ASSERT_EQUAL_PTR(text::glyphFor(f, 'M'), text::glyphFor(f, 0x041C));
}

void test_the_no_break_space_spaces() {
  for (FontId id : kIds) {
    const GfxFont& f = awtrixFont(id);
    TEST_ASSERT_EQUAL_PTR(text::glyphFor(f, ' '), text::glyphFor(f, 0x00A0));
  }
}

void test_no_covered_letter_falls_back_to_the_placeholder() {
  const std::string letters =
      "\xC4\x8D\xC5\x99\xC5\x82\xC5\xBC\xC3\xA4"
      "\xD0\x90\xD0\xB1\xD0\xB6\xD1\x8F\xD1\x91";
  for (FontId id : kIds) {
    const GfxFont& f = awtrixFont(id);
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
  for (FontId id : kIds) {
    const GfxFont& f = awtrixFont(id);
    const int degree = text::charAdvance(f, 0x00B0);
    TEST_ASSERT_TRUE(degree > 0);
    TEST_ASSERT_EQUAL_INT(degree, text::width(f, "\xC2\xB0"));
    TEST_ASSERT_EQUAL_INT(text::charAdvance(f, '2') * 2 + degree, text::width(f, "22\xC2\xB0"));
  }
}

void test_the_two_fonts_are_different() {
  const GfxFont& small = awtrixFont(FontId::Small);
  const GfxFont& large = awtrixFont(FontId::Large);
  TEST_ASSERT_TRUE(large.yAdvance > small.yAdvance);
  TEST_ASSERT_TRUE(text::glyphFor(large, 'A')->height > text::glyphFor(small, 'A')->height);
}

void test_both_fonts_cover_the_same_codepoints() {
  const std::vector<uint32_t> a = coveredCodepoints(awtrixFont(FontId::Small));
  const std::vector<uint32_t> b = coveredCodepoints(awtrixFont(FontId::Large));
  TEST_ASSERT_EQUAL_UINT(a.size(), b.size());
  for (std::size_t i = 0; i < a.size() && i < b.size(); ++i)
    TEST_ASSERT_EQUAL_UINT32(a[i], b[i]);
}

void test_an_unknown_font_id_falls_back() {
  TEST_ASSERT_EQUAL_PTR(&awtrixFont(FontId::Small), &awtrixFont(static_cast<FontId>(99)));
}

}

void setUp() {}
void tearDown() {}

int main(int, char**) {
  UNITY_BEGIN();
  RUN_TEST(test_ranges_are_ordered_and_disjoint);
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
  RUN_TEST(test_an_unknown_font_id_falls_back);
  UNITY_END();
  return 0;
}
