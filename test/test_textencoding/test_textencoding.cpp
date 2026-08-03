#include <unity.h>

#include <cstdint>
#include <string>

#include "core/render/TextEncoding.h"

using namespace awtrix;

namespace {

const uint8_t kBitmap[] = {0xE0, 0xA0, 0xC0, 0x60, 0x20, 0x90};
const FontGlyph kGlyphs[] = {
    {0, 8, 1, 4, 0, -5},
    {1, 8, 1, 4, 0, -5},
    {2, 8, 1, 4, 0, -5},
    {3, 8, 1, 3, 0, -5},
    {4, 8, 1, 5, 0, -5},
    {5, 8, 1, 6, 0, -5},
};
const uint16_t kIndex[] = {6, 3, 0};
const FontRange kRanges[] = {{0x100, 0x102, kIndex}};
const GfxFont kFont = {kBitmap, kGlyphs, 0x3F, 0x43, 8, kRanges, 1};

uint32_t decodeOne(const std::string& s) {
  std::size_t i = 0;
  return text::nextCodepoint(s, i);
}

void test_decoder_reads_each_sequence_length() {
  TEST_ASSERT_EQUAL_UINT32('A', decodeOne("A"));
  TEST_ASSERT_EQUAL_UINT32(0x00E4, decodeOne("\xC3\xA4"));
  TEST_ASSERT_EQUAL_UINT32(0x20AC, decodeOne("\xE2\x82\xAC"));
  TEST_ASSERT_EQUAL_UINT32(0x1F600, decodeOne("\xF0\x9F\x98\x80"));
}

void test_decoder_rejects_what_is_not_utf8() {
  TEST_ASSERT_EQUAL_UINT32(text::kInvalidCodepoint, decodeOne("\xC3"));
  TEST_ASSERT_EQUAL_UINT32(text::kInvalidCodepoint, decodeOne("\xC2""A"));
  TEST_ASSERT_EQUAL_UINT32(text::kInvalidCodepoint, decodeOne("\xB0"));
  TEST_ASSERT_EQUAL_UINT32(text::kInvalidCodepoint, decodeOne("\xC0\xB0"));
  TEST_ASSERT_EQUAL_UINT32(text::kInvalidCodepoint, decodeOne("\xED\xA0\x80"));
  TEST_ASSERT_EQUAL_UINT32(text::kInvalidCodepoint, decodeOne("\xF5\x80\x80\x80"));
}

void test_decoder_always_advances() {
  const std::string s = "\xB0\xB0\xB0";
  std::size_t i = 0;
  int steps = 0;
  while (i < s.size() && steps < 10) {
    text::nextCodepoint(s, i);
    ++steps;
  }
  TEST_ASSERT_EQUAL_INT(1, steps);
}

void test_is_valid_utf8_shares_the_decoder() {
  TEST_ASSERT_TRUE(text::isValidUtf8("caf\xC3\xA9"));
  TEST_ASSERT_TRUE(text::isValidUtf8(""));
  TEST_ASSERT_FALSE(text::isValidUtf8("\xC0\xB0"));
  TEST_ASSERT_FALSE(text::isValidUtf8("\xED\xA0\x80"));
  TEST_ASSERT_FALSE(text::isValidUtf8("\xE2\x82"));
}

void test_glyph_lookup_covers_dense_and_sparse() {
  TEST_ASSERT_EQUAL_PTR(&kGlyphs[2], text::glyphFor(kFont, 'A'));
  TEST_ASSERT_EQUAL_PTR(&kGlyphs[5], text::glyphFor(kFont, 0x100));
  TEST_ASSERT_NULL(text::glyphFor(kFont, 0x102));
  TEST_ASSERT_NULL(text::glyphFor(kFont, 0x200));
}

void test_a_folded_codepoint_resolves_to_its_twin() {
  TEST_ASSERT_EQUAL_PTR(text::glyphFor(kFont, 'A'), text::glyphFor(kFont, 0x101));
}

void test_iterator_walks_glyphs_not_bytes() {
  TEST_ASSERT_EQUAL_UINT(2u, (unsigned)text::glyphCount(kFont, "A\xC4\x80"));
  TEST_ASSERT_EQUAL_UINT(0u, (unsigned)text::glyphCount(kFont, ""));

  text::GlyphIter it(kFont, "A\xC4\x80");
  const FontGlyph* g = nullptr;
  TEST_ASSERT_TRUE(it.next(g));
  TEST_ASSERT_EQUAL_PTR(&kGlyphs[2], g);
  TEST_ASSERT_TRUE(it.next(g));
  TEST_ASSERT_EQUAL_PTR(&kGlyphs[5], g);
  TEST_ASSERT_FALSE(it.next(g));
}

void test_iterator_substitutes_the_placeholder() {
  text::GlyphIter it(kFont, "\xF0\x9F\x98\x80");
  const FontGlyph* g = nullptr;
  TEST_ASSERT_TRUE(it.next(g));
  TEST_ASSERT_EQUAL_PTR(&kGlyphs[0], g);
  TEST_ASSERT_FALSE(it.next(g));

  TEST_ASSERT_EQUAL_UINT(1u, (unsigned)text::glyphCount(kFont, "\xC4\x82"));
}

void test_uppercase_reaches_past_ascii() {
  TEST_ASSERT_EQUAL_STRING("HELLO", text::toUpperUtf8("hello").c_str());
  TEST_ASSERT_EQUAL_STRING("\xC3\x84", text::toUpperUtf8("\xC3\xA4").c_str());
  TEST_ASSERT_EQUAL_STRING("\xC4\x8C", text::toUpperUtf8("\xC4\x8D").c_str());
  TEST_ASSERT_EQUAL_STRING("\xC5\xBB", text::toUpperUtf8("\xC5\xBC").c_str());
  TEST_ASSERT_EQUAL_STRING("\xD0\x9F", text::toUpperUtf8("\xD0\xBF").c_str());
  TEST_ASSERT_EQUAL_STRING("\xD0\x81", text::toUpperUtf8("\xD1\x91").c_str());
  TEST_ASSERT_EQUAL_STRING("\xCE\x91", text::toUpperUtf8("\xCE\xB1").c_str());
}

void test_uppercase_special_cases() {
  TEST_ASSERT_EQUAL_STRING("\xCE\xA3", text::toUpperUtf8("\xCF\x82").c_str());
  TEST_ASSERT_EQUAL_STRING("\xCE\xA3", text::toUpperUtf8("\xCF\x83").c_str());
  TEST_ASSERT_EQUAL_STRING("\xC3\x9F", text::toUpperUtf8("\xC3\x9F").c_str());
  TEST_ASSERT_EQUAL_STRING("\xC3\xB7", text::toUpperUtf8("\xC3\xB7").c_str());
  TEST_ASSERT_EQUAL_STRING("I", text::toUpperUtf8("\xC4\xB1").c_str());
}

void test_uppercase_passes_broken_input_through() {
  TEST_ASSERT_EQUAL_STRING("A\xC3Z", text::toUpperUtf8("a\xC3z").c_str());
}

void test_stream_bytes_pick_an_encoding() {
  TEST_ASSERT_EQUAL_STRING("caf\xC3\xA9", text::fromStreamBytes("caf\xC3\xA9").c_str());
  TEST_ASSERT_EQUAL_STRING("caf\xC3\xA9", text::fromStreamBytes("caf\xE9").c_str());
  TEST_ASSERT_EQUAL_STRING("ab", text::fromStreamBytes("a\x01" "b").c_str());
}

}

void setUp() {}
void tearDown() {}

int main(int, char**) {
  UNITY_BEGIN();
  RUN_TEST(test_decoder_reads_each_sequence_length);
  RUN_TEST(test_decoder_rejects_what_is_not_utf8);
  RUN_TEST(test_decoder_always_advances);
  RUN_TEST(test_is_valid_utf8_shares_the_decoder);
  RUN_TEST(test_glyph_lookup_covers_dense_and_sparse);
  RUN_TEST(test_a_folded_codepoint_resolves_to_its_twin);
  RUN_TEST(test_iterator_walks_glyphs_not_bytes);
  RUN_TEST(test_iterator_substitutes_the_placeholder);
  RUN_TEST(test_uppercase_reaches_past_ascii);
  RUN_TEST(test_uppercase_special_cases);
  RUN_TEST(test_uppercase_passes_broken_input_through);
  RUN_TEST(test_stream_bytes_pick_an_encoding);
  UNITY_END();
  return 0;
}
