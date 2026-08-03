#include <unity.h>

#include <initializer_list>

#include "core/render/MatrixLayout.h"

using namespace awtrix;

void setUp() {}
void tearDown() {}

namespace {

int refUlanzi(int x, int y, int W) { return y * W + ((y % 2 == 0) ? x : (W - 1 - x)); }
int refTiles(int x, int y) { return (x / 8) * 64 + y * 8 + (x % 8); }
int refColumns(int x, int y, int H) { return x * H + ((x % 2 == 0) ? y : (H - 1 - y)); }

constexpr int kMaxLeds = kMatrixWidthMax * kMatrixHeight;

struct Grid {
  int panelWidth;
  int panels;
};

constexpr Grid kGrids[] = {{1, 32},  {1, 64},  {1, 128}, {2, 16}, {2, 64}, {4, 8},  {4, 32},
                           {5, 9},   {8, 4},   {8, 16},  {10, 4}, {16, 2}, {16, 8}, {32, 1},
                           {32, 4},  {33, 1},  {64, 1},  {64, 2}, {128, 1}};

constexpr PanelStart kStarts[] = {PanelStart::TopLeft, PanelStart::TopRight, PanelStart::BottomLeft,
                                  PanelStart::BottomRight};

MatrixLayout ulanzi() { return MatrixLayout{}; }

MatrixLayout tiles8x8() {
  MatrixLayout l;
  l.panelWidth = 8;
  l.panels = 4;
  l.panelSerpentine = false;
  return l;
}

MatrixLayout columns() {
  MatrixLayout l;
  l.panelWiring = Wiring::Columns;
  return l;
}

void assertBijection(const MatrixLayout& layout) {
  const int n = layout.ledCount();
  TEST_ASSERT_TRUE(n > 0 && n <= kMaxLeds);
  int seen[kMaxLeds] = {0};
  for (int y = 0; y < layout.height(); ++y) {
    for (int x = 0; x < layout.width(); ++x) {
      const int idx = layout.xyToIndex(x, y);
      TEST_ASSERT_TRUE(idx >= 0 && idx < n);
      seen[idx]++;
    }
  }
  for (int i = 0; i < n; ++i) TEST_ASSERT_EQUAL_INT(1, seen[i]);
}

void assertIsDefault(const MatrixLayout& layout) {
  const MatrixLayout d;
  TEST_ASSERT_EQUAL_INT(d.panelWidth, layout.panelWidth);
  TEST_ASSERT_EQUAL_INT(d.panels, layout.panels);
  TEST_ASSERT_EQUAL_INT(static_cast<int>(d.panelStart), static_cast<int>(layout.panelStart));
  TEST_ASSERT_EQUAL_INT(static_cast<int>(d.panelWiring), static_cast<int>(layout.panelWiring));
  TEST_ASSERT_EQUAL_INT(d.panelSerpentine, layout.panelSerpentine);
}

}


static void test_default_layout_is_the_ulanzi_wiring() {
  const MatrixLayout layout = ulanzi();
  TEST_ASSERT_EQUAL_INT(32, layout.width());
  TEST_ASSERT_EQUAL_INT(8, layout.height());
  TEST_ASSERT_EQUAL_INT(256, layout.ledCount());
  for (int y = 0; y < 8; ++y)
    for (int x = 0; x < 32; ++x)
      TEST_ASSERT_EQUAL_INT(refUlanzi(x, y, 32), layout.xyToIndex(x, y));
}

static void test_four_chained_8x8_panels_match_reference() {
  const MatrixLayout layout = tiles8x8();
  TEST_ASSERT_EQUAL_INT(32, layout.width());
  for (int y = 0; y < 8; ++y)
    for (int x = 0; x < 32; ++x)
      TEST_ASSERT_EQUAL_INT(refTiles(x, y), layout.xyToIndex(x, y));
}

static void test_column_wired_panel_matches_reference() {
  const MatrixLayout layout = columns();
  for (int y = 0; y < 8; ++y)
    for (int x = 0; x < 32; ++x)
      TEST_ASSERT_EQUAL_INT(refColumns(x, y, 8), layout.xyToIndex(x, y));
}

static void test_wide_panels_stay_bijections() {
  for (int w : {64, 96, 128}) {
    MatrixLayout rows;
    rows.panelWidth = w;
    assertBijection(rows);
    MatrixLayout cols = columns();
    cols.panelWidth = w;
    assertBijection(cols);
    MatrixLayout chained = tiles8x8();
    chained.panels = w / 8;
    assertBijection(chained);
  }
}

static void test_top_right_start_reverses_every_run() {
  MatrixLayout layout;
  layout.panelStart = PanelStart::TopRight;
  TEST_ASSERT_EQUAL_INT(0, layout.xyToIndex(31, 0));
  TEST_ASSERT_EQUAL_INT(31, layout.xyToIndex(0, 0));
  TEST_ASSERT_EQUAL_INT(32, layout.xyToIndex(0, 1));
  TEST_ASSERT_EQUAL_INT(63, layout.xyToIndex(31, 1));
  assertBijection(layout);
}

static void test_bottom_left_start_reverses_the_run_order() {
  MatrixLayout layout;
  layout.panelStart = PanelStart::BottomLeft;
  TEST_ASSERT_EQUAL_INT(0, layout.xyToIndex(0, 7));
  TEST_ASSERT_EQUAL_INT(31, layout.xyToIndex(31, 7));
  TEST_ASSERT_EQUAL_INT(32, layout.xyToIndex(31, 6));
  TEST_ASSERT_EQUAL_INT(224, layout.xyToIndex(31, 0));
  TEST_ASSERT_EQUAL_INT(255, layout.xyToIndex(0, 0));
  assertBijection(layout);
}

static void test_bottom_right_start_reverses_both() {
  MatrixLayout layout;
  layout.panelStart = PanelStart::BottomRight;
  TEST_ASSERT_EQUAL_INT(0, layout.xyToIndex(31, 7));
  TEST_ASSERT_EQUAL_INT(31, layout.xyToIndex(0, 7));
  TEST_ASSERT_EQUAL_INT(32, layout.xyToIndex(0, 6));
  TEST_ASSERT_EQUAL_INT(255, layout.xyToIndex(31, 0));
  assertBijection(layout);
}

static void test_progressive_panel_runs_ignore_serpentine() {
  MatrixLayout layout;
  layout.panelSerpentine = false;
  for (int y = 0; y < 8; ++y)
    for (int x = 0; x < 32; ++x) TEST_ASSERT_EQUAL_INT(y * 32 + x, layout.xyToIndex(x, y));
}

static void test_widths_that_are_not_multiples_of_eight_are_valid() {
  MatrixLayout odd;
  odd.panelWidth = 33;
  TEST_ASSERT_EQUAL_INT(33, odd.width());
  assertBijection(odd);

  MatrixLayout chained;
  chained.panelWidth = 5;
  chained.panels = 9;
  TEST_ASSERT_EQUAL_INT(45, chained.width());
  assertBijection(chained);
}

static void test_bijection_over_the_valid_config_space() {
  for (const Grid& g : kGrids) {
    for (PanelStart start : kStarts) {
      for (int wiring = 0; wiring < 2; ++wiring) {
        for (int panelSerp = 0; panelSerp < 2; ++panelSerp) {
          MatrixLayout layout;
          layout.panelWidth = g.panelWidth;
          layout.panels = g.panels;
          layout.panelStart = start;
          layout.panelWiring = wiring ? Wiring::Columns : Wiring::Rows;
          layout.panelSerpentine = panelSerp != 0;
          TEST_ASSERT_EQUAL_INT(kMatrixHeight, layout.height());
          TEST_ASSERT_TRUE(layout.width() >= kMatrixWidthMin &&
                           layout.width() <= kMatrixWidthMax);
          assertBijection(layout);
        }
      }
    }
  }
}

static void test_mirror_is_horizontal_flip() {
  const MatrixLayout plain;
  MatrixLayout mirror;
  mirror.mirror = true;
  for (int y = 0; y < 8; ++y)
    for (int x = 0; x < 32; ++x)
      TEST_ASSERT_EQUAL_INT(plain.xyToIndex(31 - x, y), mirror.xyToIndex(x, y));
  assertBijection(mirror);
}

static void test_rotate180_flips_both_axes() {
  const MatrixLayout plain;
  MatrixLayout rot;
  rot.rotate180 = true;
  for (int y = 0; y < 8; ++y)
    for (int x = 0; x < 32; ++x)
      TEST_ASSERT_EQUAL_INT(plain.xyToIndex(31 - x, 7 - y), rot.xyToIndex(x, y));
  assertBijection(rot);
}

static void test_mirror_and_rotate_compose_to_vertical_flip() {
  const MatrixLayout plain;
  MatrixLayout both;
  both.mirror = true;
  both.rotate180 = true;
  for (int y = 0; y < 8; ++y)
    for (int x = 0; x < 32; ++x)
      TEST_ASSERT_EQUAL_INT(plain.xyToIndex(x, 7 - y), both.xyToIndex(x, y));
}

static void test_display_transforms_survive_every_wiring() {
  for (PanelStart start : kStarts) {
    MatrixLayout layout = tiles8x8();
    layout.panelStart = start;
    layout.mirror = true;
    layout.rotate180 = true;
    assertBijection(layout);
  }
}

static void test_sanitize_keeps_a_valid_layout() {
  MatrixLayout in = tiles8x8();
  bool changed = true;
  const MatrixLayout out = sanitizeMatrixLayout(in, &changed);
  TEST_ASSERT_FALSE(changed);
  TEST_ASSERT_EQUAL_INT(8, out.panelWidth);
  TEST_ASSERT_EQUAL_INT(4, out.panels);
  TEST_ASSERT_FALSE(out.panelSerpentine);
}

static void test_height_is_always_eight() {
  MatrixLayout wide;
  wide.panelWidth = 64;
  TEST_ASSERT_EQUAL_INT(8, wide.height());
  TEST_ASSERT_EQUAL_INT(512, wide.ledCount());
  TEST_ASSERT_EQUAL_INT(8, tiles8x8().height());
}

static void test_sanitize_rejects_a_width_outside_the_envelope() {
  MatrixLayout wide;
  wide.panelWidth = 128;
  wide.panels = 16;
  assertIsDefault(sanitizeMatrixLayout(wide));

  MatrixLayout narrow;
  narrow.panelWidth = 16;
  assertIsDefault(sanitizeMatrixLayout(narrow));
}

static void test_sanitize_clamps_nonsense_fields() {
  MatrixLayout in;
  in.panelWidth = 0;
  in.panels = -4;
  const MatrixLayout out = sanitizeMatrixLayout(in);
  assertIsDefault(out);
}

static void test_sanitize_keeps_the_display_transforms() {
  MatrixLayout in;
  in.panelWidth = 4;
  in.mirror = true;
  in.rotate180 = true;
  const MatrixLayout out = sanitizeMatrixLayout(in);
  assertIsDefault(out);
  TEST_ASSERT_TRUE(out.mirror);
  TEST_ASSERT_TRUE(out.rotate180);
}

static void test_sanitize_resets_out_of_range_enums() {
  MatrixLayout in;
  in.panelStart = static_cast<PanelStart>(9);
  in.panelWiring = static_cast<Wiring>(7);
  bool changed = false;
  const MatrixLayout out = sanitizeMatrixLayout(in, &changed);
  TEST_ASSERT_TRUE(changed);
  TEST_ASSERT_EQUAL_INT(static_cast<int>(PanelStart::TopLeft), static_cast<int>(out.panelStart));
  TEST_ASSERT_EQUAL_INT(static_cast<int>(Wiring::Rows), static_cast<int>(out.panelWiring));
}

static void test_enum_name_tables_cover_every_value() {
  TEST_ASSERT_EQUAL_INT(4, kPanelStartCount);
  TEST_ASSERT_EQUAL_INT(2, kWiringCount);
  TEST_ASSERT_EQUAL_STRING("topLeft", kPanelStartNames[0]);
  TEST_ASSERT_EQUAL_STRING("topRight", kPanelStartNames[1]);
  TEST_ASSERT_EQUAL_STRING("bottomLeft", kPanelStartNames[2]);
  TEST_ASSERT_EQUAL_STRING("bottomRight", kPanelStartNames[3]);
  TEST_ASSERT_EQUAL_STRING("rows", kWiringNames[0]);
  TEST_ASSERT_EQUAL_STRING("columns", kWiringNames[1]);
}

int main(int, char**) {
  UNITY_BEGIN();
  RUN_TEST(test_default_layout_is_the_ulanzi_wiring);
  RUN_TEST(test_four_chained_8x8_panels_match_reference);
  RUN_TEST(test_column_wired_panel_matches_reference);
  RUN_TEST(test_wide_panels_stay_bijections);
  RUN_TEST(test_top_right_start_reverses_every_run);
  RUN_TEST(test_bottom_left_start_reverses_the_run_order);
  RUN_TEST(test_bottom_right_start_reverses_both);
  RUN_TEST(test_progressive_panel_runs_ignore_serpentine);
  RUN_TEST(test_widths_that_are_not_multiples_of_eight_are_valid);
  RUN_TEST(test_bijection_over_the_valid_config_space);
  RUN_TEST(test_mirror_is_horizontal_flip);
  RUN_TEST(test_rotate180_flips_both_axes);
  RUN_TEST(test_mirror_and_rotate_compose_to_vertical_flip);
  RUN_TEST(test_display_transforms_survive_every_wiring);
  RUN_TEST(test_sanitize_keeps_a_valid_layout);
  RUN_TEST(test_height_is_always_eight);
  RUN_TEST(test_sanitize_rejects_a_width_outside_the_envelope);
  RUN_TEST(test_sanitize_clamps_nonsense_fields);
  RUN_TEST(test_sanitize_keeps_the_display_transforms);
  RUN_TEST(test_sanitize_resets_out_of_range_enums);
  RUN_TEST(test_enum_name_tables_cover_every_value);
  return UNITY_END();
}
