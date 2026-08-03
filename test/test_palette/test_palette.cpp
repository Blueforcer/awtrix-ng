#include <unity.h>

#include <memory>
#include <string>

#include "core/StrCase.h"
#include "core/render/ColorRamp.h"
#include "core/render/PaletteFile.h"
#include "core/render/Palette.h"
#include "core/render/PaletteStore.h"

using namespace awtrix::render;

void setUp() {}
void tearDown() {}

static void test_stock_lookup_is_case_insensitive() {
  TEST_ASSERT_TRUE(findStockPalette("ocean") == findStockPalette("Ocean"));
  TEST_ASSERT_TRUE(findStockPalette("RAINBOW") == findStockPalette("Rainbow"));
  TEST_ASSERT_NOT_NULL(findStockPalette("lAvA"));
  TEST_ASSERT_NULL(findStockPalette("Nope"));
}

static void test_named_palettes_match_fastled() {
  TEST_ASSERT_EQUAL_HEX32(0x0000FFu, namedPalette("Cloud").entries[0]);
  TEST_ASSERT_EQUAL_HEX32(0x000000u, namedPalette("Lava").entries[0]);
  TEST_ASSERT_EQUAL_HEX32(0x191970u, namedPalette("Ocean").entries[0]);
  TEST_ASSERT_EQUAL_HEX32(0x006400u, namedPalette("Forest").entries[0]);
  TEST_ASSERT_EQUAL_HEX32(0xFF0000u, namedPalette("Stripe").entries[0]);
  TEST_ASSERT_EQUAL_HEX32(0x5500ABu, namedPalette("Party").entries[0]);
  TEST_ASSERT_EQUAL_HEX32(0x000000u, namedPalette("Heat").entries[0]);
  TEST_ASSERT_EQUAL_HEX32(0xFFFFFFu, namedPalette("Heat").entries[15]);
}

static void test_unknown_name_falls_back_to_rainbow() {
  TEST_ASSERT_EQUAL_HEX32(0xFF0000u, namedPalette("NoSuchPalette").entries[0]);
  TEST_ASSERT_EQUAL_HEX32(0xD5002Bu, namedPalette("NoSuchPalette").entries[15]);
}

static void test_default_is_ocean() {
  TEST_ASSERT_EQUAL_HEX32(namedPalette("Ocean").entries[0], defaultPalette().entries[0]);
}

static void test_exact_entry_on_nibble_boundary() {
  const Palette& p = namedPalette("Heat");
  TEST_ASSERT_EQUAL_HEX32(p.entries[0], colorFromPalette(p, 0, true));
  TEST_ASSERT_EQUAL_HEX32(p.entries[1], colorFromPalette(p, 16, true));
  TEST_ASSERT_EQUAL_HEX32(p.entries[15], colorFromPalette(p, 240, true));
  TEST_ASSERT_EQUAL_HEX32(p.entries[15], colorFromPalette(p, 240, false));
}

static void test_noblend_snaps_to_entry() {
  const Palette& p = namedPalette("Heat");
  TEST_ASSERT_EQUAL_HEX32(p.entries[1], colorFromPalette(p, 16, false));
  TEST_ASSERT_EQUAL_HEX32(p.entries[1], colorFromPalette(p, 24, false));
  TEST_ASSERT_EQUAL_HEX32(p.entries[1], colorFromPalette(p, 31, false));
}

static void test_linearblend_interpolates() {
  const Palette& p = namedPalette("Heat");
  TEST_ASSERT_EQUAL_HEX32(0x4C0000u, colorFromPalette(p, 16 + 8, true));
}

static void test_blend_wraps_from_last_to_first() {
  Palette p{};
  for (auto& e : p.entries) e = 0x000000u;
  p.entries[15] = 0x000000u;
  p.entries[0] = 0xFF0000u;
  TEST_ASSERT_EQUAL_HEX32(0x800000u, colorFromPalette(p, 240 + 8, true));
}

static void test_stops_expand_across_all_entries() {
  const uint32_t two[2] = {0xFF0000u, 0x0000FFu};
  const Palette p = paletteFromStops(two, 2);
  TEST_ASSERT_EQUAL_HEX32(0xFF0000u, p.entries[0]);
  TEST_ASSERT_EQUAL_HEX32(0x0000FFu, p.entries[15]);
  for (int i = 1; i < 16; ++i) {
    TEST_ASSERT_TRUE(((p.entries[i] >> 16) & 0xFF) <= ((p.entries[i - 1] >> 16) & 0xFF));
    TEST_ASSERT_TRUE((p.entries[i] & 0xFF) >= (p.entries[i - 1] & 0xFF));
  }
}

static void test_one_stop_fills_the_table() {
  const uint32_t one[1] = {0x123456u};
  const Palette p = paletteFromStops(one, 1);
  TEST_ASSERT_EQUAL_HEX32(0x123456u, p.entries[0]);
  TEST_ASSERT_EQUAL_HEX32(0x123456u, p.entries[15]);
}

static void test_sixteen_stops_are_kept_verbatim() {
  uint32_t all[16];
  for (int i = 0; i < 16; ++i) all[i] = static_cast<uint32_t>(i * 0x010101);
  const Palette p = paletteFromStops(all, 16);
  for (int i = 0; i < 16; ++i) TEST_ASSERT_EQUAL_HEX32(all[i], p.entries[i]);
}

static void test_end_positions_reproduce_even_spacing() {
  const uint32_t even[3] = {0xFF0000u, 0x00FF00u, 0x0000FFu};
  const PaletteStop placed[3] = {{0xFF0000u, 0}, {0x00FF00u, 50}, {0x0000FFu, 100}};
  const Palette a = paletteFromStops(even, 3);
  const Palette b = paletteFromPositionedStops(placed, 3);
  for (int i = 0; i < 16; ++i) TEST_ASSERT_EQUAL_HEX32(a.entries[i], b.entries[i]);
}

static void test_positions_move_the_ramp() {
  const PaletteStop s[2] = {{0xFF0000u, 75}, {0x0000FFu, 100}};
  TEST_ASSERT_EQUAL_HEX32(0xFF0000u, paletteFromPositionedStops(s, 2).entries[0]);
  TEST_ASSERT_EQUAL_HEX32(0xFF0000u, paletteFromPositionedStops(s, 2).entries[11]);
  TEST_ASSERT_EQUAL_HEX32(0x0000FFu, paletteFromPositionedStops(s, 2).entries[15]);
  const uint32_t mid = paletteFromPositionedStops(s, 2).entries[13];
  TEST_ASSERT_EQUAL_HEX32(0x88u, (mid >> 16) & 0xFF);
  TEST_ASSERT_EQUAL_HEX32(0x77u, mid & 0xFF);
}

static void test_ends_extend_flat() {
  const PaletteStop s[2] = {{0x00FF00u, 40}, {0xFFFFFFu, 60}};
  const Palette p = paletteFromPositionedStops(s, 2);
  TEST_ASSERT_EQUAL_HEX32(0x00FF00u, p.entries[0]);
  TEST_ASSERT_EQUAL_HEX32(0x00FF00u, p.entries[6]);
  TEST_ASSERT_EQUAL_HEX32(0xFFFFFFu, p.entries[10]);
  TEST_ASSERT_EQUAL_HEX32(0xFFFFFFu, p.entries[15]);
}

static void test_shared_position_is_a_hard_edge() {
  const PaletteStop s[3] = {{0xFF0000u, 0}, {0xFF0000u, 50}, {0x0000FFu, 50}};
  const Palette p = paletteFromPositionedStops(s, 3);
  TEST_ASSERT_EQUAL_HEX32(0xFF0000u, p.entries[7]);
  TEST_ASSERT_EQUAL_HEX32(0x0000FFu, p.entries[8]);
}

static void test_single_positioned_stop_fills_the_table() {
  const PaletteStop s[1] = {{0x123456u, 30}};
  const Palette p = paletteFromPositionedStops(s, 1);
  TEST_ASSERT_EQUAL_HEX32(0x123456u, p.entries[0]);
  TEST_ASSERT_EQUAL_HEX32(0x123456u, p.entries[15]);
}

static void test_file_without_positions_is_evenly_spread() {
  Palette p{};
  TEST_ASSERT_TRUE(parsePaletteFile("FF0000\n0000FF\n", p));
  const uint32_t two[2] = {0xFF0000u, 0x0000FFu};
  const Palette even = paletteFromStops(two, 2);
  for (int i = 0; i < 16; ++i) TEST_ASSERT_EQUAL_HEX32(even.entries[i], p.entries[i]);
}

static void test_file_tolerates_hashes_blanks_and_crlf() {
  Palette p{};
  TEST_ASSERT_TRUE(parsePaletteFile("#FF0000\r\n\r\n  0000FF  \r\n", p));
  TEST_ASSERT_EQUAL_HEX32(0xFF0000u, p.entries[0]);
  TEST_ASSERT_EQUAL_HEX32(0x0000FFu, p.entries[15]);
}

static void test_file_reads_positions() {
  Palette p{};
  TEST_ASSERT_TRUE(parsePaletteFile("FF0000@75\n0000FF@100\n", p));
  TEST_ASSERT_EQUAL_HEX32(0xFF0000u, p.entries[11]);
  TEST_ASSERT_EQUAL_HEX32(0x0000FFu, p.entries[15]);
}

static void test_file_sorts_stops_by_position() {
  Palette p{};
  TEST_ASSERT_TRUE(parsePaletteFile("0000FF@100\nFF0000@0\n", p));
  TEST_ASSERT_EQUAL_HEX32(0xFF0000u, p.entries[0]);
  TEST_ASSERT_EQUAL_HEX32(0x0000FFu, p.entries[15]);
}

static void test_file_refuses_partial_positions() {
  Palette p{};
  TEST_ASSERT_FALSE(parsePaletteFile("FF0000@0\n0000FF\n", p));
}

static void test_file_refuses_malformed_lines() {
  Palette p{};
  TEST_ASSERT_FALSE(parsePaletteFile("", p));
  TEST_ASSERT_FALSE(parsePaletteFile("\n\n", p));
  TEST_ASSERT_FALSE(parsePaletteFile("FF00\n", p));
  TEST_ASSERT_FALSE(parsePaletteFile("GGGGGG\n", p));
  TEST_ASSERT_FALSE(parsePaletteFile("FF0000@\n", p));
  TEST_ASSERT_FALSE(parsePaletteFile("FF0000@101\n", p));
  TEST_ASSERT_FALSE(parsePaletteFile("FF0000@1x\n", p));
}

static void test_file_stops_at_sixteen() {
  std::string text;
  for (int i = 0; i < 20; ++i) text += "0000FF\n";
  Palette p{};
  TEST_ASSERT_TRUE(parsePaletteFile(text, p));
  TEST_ASSERT_EQUAL_HEX32(0x0000FFu, p.entries[15]);
}

static void test_normalised_sampling_ends_on_the_last_entry() {
  const uint32_t two[2] = {0xFF0000u, 0x0000FFu};
  ColorRamp r;
  r.pal = std::make_shared<const Palette>(paletteFromStops(two, 2));
  TEST_ASSERT_EQUAL_HEX32(0xFF0000u, r.at(0.0f));
  TEST_ASSERT_EQUAL_HEX32(0x0000FFu, r.at(1.0f));
  TEST_ASSERT_EQUAL_HEX32(0xFF0000u, r.at(-3.0f));
  TEST_ASSERT_EQUAL_HEX32(0x0000FFu, r.at(4.0f));
}

static void test_invalid_ramp_reports_itself() {
  ColorRamp r;
  TEST_ASSERT_FALSE(r.valid());
  TEST_ASSERT_EQUAL_HEX32(defaultPalette().entries[0], r.atIndex(0));
}

static void test_origin_is_zero_while_still() {
  ColorRamp r;
  r.pal = std::make_shared<const Palette>(namedPalette("Heat"));
  TEST_ASSERT_EQUAL_INT(0, r.originAt(123456, 32));
}

static void test_origin_advances_and_wraps() {
  ColorRamp r;
  r.pal = std::make_shared<const Palette>(namedPalette("Heat"));
  r.speed = 1.0f;
  r.spanPx = 32;
  TEST_ASSERT_EQUAL_INT(16, r.originAt(500, 8));
  TEST_ASSERT_EQUAL_INT(0, r.originAt(1000, 8));
  TEST_ASSERT_EQUAL_INT(0, r.originAt(9000, 8));
  r.spanPx = 0;
  TEST_ASSERT_EQUAL_INT(4, r.originAt(500, 8));
}

static void test_named_palettes_are_interned() {
  const std::shared_ptr<const Palette> a = paletteByName("Heat");
  const std::shared_ptr<const Palette> b = paletteByName("heat");
  TEST_ASSERT_NOT_NULL(a.get());
  TEST_ASSERT_EQUAL_PTR(a.get(), b.get());
  TEST_ASSERT_NULL(paletteByName("nosuchpalette").get());
  TEST_ASSERT_NULL(paletteByName("").get());
}

static void test_loader_supplies_unknown_names() {
  setPaletteLoader([](const std::string& name, Palette& out) {
    if (name != "mine") return false;
    for (auto& e : out.entries) e = 0xABCDEFu;
    return true;
  });
  const std::shared_ptr<const Palette> p = paletteByName("mine");
  TEST_ASSERT_NOT_NULL(p.get());
  TEST_ASSERT_EQUAL_HEX32(0xABCDEFu, p->entries[0]);
  TEST_ASSERT_NULL(paletteByName("other").get());
  setPaletteLoader(nullptr);
  clearPaletteCache();
}

static void test_a_file_overrides_the_builtin_of_the_same_name() {
  setPaletteLoader([](const std::string& name, Palette& out) {
    if (!awtrix::strcase::equalsIgnoreCase(name, "Heat")) return false;
    for (auto& e : out.entries) e = 0x00FF00u;
    return true;
  });
  const std::shared_ptr<const Palette> mine = paletteByName("Heat");
  TEST_ASSERT_NOT_NULL(mine.get());
  TEST_ASSERT_EQUAL_HEX32(0x00FF00u, mine->entries[0]);
  TEST_ASSERT_EQUAL_HEX32(0x000000u, findStockPalette("Heat")->entries[0]);
  TEST_ASSERT_EQUAL_HEX32(0x191970u, paletteByName("Ocean")->entries[0]);
}

static void test_dropping_the_file_restores_the_builtin() {
  setPaletteLoader(nullptr);
  clearPaletteCache();
  TEST_ASSERT_EQUAL_HEX32(0x000000u, paletteByName("Heat")->entries[0]);
  TEST_ASSERT_EQUAL_HEX32(0xFFFFFFu, paletteByName("Heat")->entries[15]);
}

static void test_dropped_user_palettes_are_released() {
  setPaletteLoader([](const std::string&, Palette& out) {
    for (auto& e : out.entries) e = 0x010203u;
    return true;
  });
  std::weak_ptr<const Palette> weak;
  {
    const std::shared_ptr<const Palette> p = paletteByName("temporary");
    weak = p;
    TEST_ASSERT_FALSE(weak.expired());
  }
  TEST_ASSERT_TRUE(weak.expired());
  setPaletteLoader(nullptr);
  clearPaletteCache();
}

int main(int, char**) {
  UNITY_BEGIN();
  RUN_TEST(test_named_palettes_match_fastled);
  RUN_TEST(test_stock_lookup_is_case_insensitive);
  RUN_TEST(test_unknown_name_falls_back_to_rainbow);
  RUN_TEST(test_default_is_ocean);
  RUN_TEST(test_exact_entry_on_nibble_boundary);
  RUN_TEST(test_noblend_snaps_to_entry);
  RUN_TEST(test_linearblend_interpolates);
  RUN_TEST(test_blend_wraps_from_last_to_first);
  RUN_TEST(test_stops_expand_across_all_entries);
  RUN_TEST(test_one_stop_fills_the_table);
  RUN_TEST(test_sixteen_stops_are_kept_verbatim);
  RUN_TEST(test_end_positions_reproduce_even_spacing);
  RUN_TEST(test_positions_move_the_ramp);
  RUN_TEST(test_ends_extend_flat);
  RUN_TEST(test_shared_position_is_a_hard_edge);
  RUN_TEST(test_single_positioned_stop_fills_the_table);
  RUN_TEST(test_file_without_positions_is_evenly_spread);
  RUN_TEST(test_file_tolerates_hashes_blanks_and_crlf);
  RUN_TEST(test_file_reads_positions);
  RUN_TEST(test_file_sorts_stops_by_position);
  RUN_TEST(test_file_refuses_partial_positions);
  RUN_TEST(test_file_refuses_malformed_lines);
  RUN_TEST(test_file_stops_at_sixteen);
  RUN_TEST(test_normalised_sampling_ends_on_the_last_entry);
  RUN_TEST(test_invalid_ramp_reports_itself);
  RUN_TEST(test_origin_is_zero_while_still);
  RUN_TEST(test_origin_advances_and_wraps);
  RUN_TEST(test_named_palettes_are_interned);
  RUN_TEST(test_loader_supplies_unknown_names);
  RUN_TEST(test_a_file_overrides_the_builtin_of_the_same_name);
  RUN_TEST(test_dropping_the_file_restores_the_builtin);
  RUN_TEST(test_dropped_user_palettes_are_released);
  return UNITY_END();
}
