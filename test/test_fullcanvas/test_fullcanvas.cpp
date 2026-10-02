#include <unity.h>
#include <vector>
#include "core/apps/IApp.h"
#include "core/apps/SpecRenderer.h"
#include "core/effects/effects/FadeEffect.h"
#include "core/effects/effects/MoreEffects.h"
#include "core/effects/overlays/WeatherOverlays.h"
#include "core/render/Gfx2d.h"
#include "core/render/TransitionComposer.h"
#include "core/script/BerryVM.h"
#include "core/script/ScriptBindings.h"

using namespace awtrix;
using namespace awtrix::render;
void setUp() {}
void tearDown() {}
namespace {
constexpr uint32_t guard = 0xDEADBEEF;
struct Panel {
  std::vector<uint32_t> pixels = std::vector<uint32_t>(53 * 11 + 2, guard);
  Canvas c{53, 11, pixels.data() + 1};
  Panel() { c.clear(); }
  void check() {
    TEST_ASSERT_EQUAL_HEX32(guard, pixels.front());
    TEST_ASSERT_EQUAL_HEX32(guard, pixels.back());
  }
  void everyRow() {
    for (int y = 0; y < c.height(); ++y) {
      bool lit = false;
      for (int x = 0; x < c.width(); ++x) lit |= c.getPixel(x, y) != 0;
      TEST_ASSERT_TRUE_MESSAGE(lit, "full canvas must reach every row");
    }
    check();
  }
};
const FontGlyph glyph[] = {{0, 1, 1, 1, 0, 0}};
const uint8_t bits[] = {0x80};
const GfxFont font{bits, glyph, 'A', 'A', 8};
}
static void test_charts_and_progress() {
  Panel p;
  for (bool autoscale : {false, true}) {
    p.c.clear();
    drawBars(p.c, {8}, 0xFFFFFFu, autoscale, 0);
    for (std::size_t i = 0; i < p.c.size(); ++i)
      TEST_ASSERT_EQUAL_HEX32(0xFFFFFFu, p.c.data()[i]);
    p.everyRow();
    p.c.clear();
    drawLineChart(p.c, {0, 8}, 0xFFFFFFu, autoscale, 0);
    TEST_ASSERT_EQUAL_HEX32(0xFFFFFFu, p.c.getPixel(0, 10));
    TEST_ASSERT_EQUAL_HEX32(0xFFFFFFu, p.c.getPixel(52, 0));
    p.everyRow();
  }
  p.c.clear();
  drawProgress(p.c, 50, 0x00FF00u, 0xFFFFFFu, 0);
  for (int y = 0; y < 11; ++y)
    for (int x = 0; x < 53; ++x)
      TEST_ASSERT_EQUAL_HEX32(y != 10 ? 0u : x < 26 ? 0x00FF00u : 0xFFFFFFu,
                             p.c.getPixel(x, y));
  p.check();
}
static void test_effect_overlay_and_eyes() {
  Panel p;
  FadeEffect effect;
  FrostOverlay overlay;
  effect.render(p.c, 0);
  const uint32_t background = p.c.getPixel(0, 5);
  overlay.render(p.c, 0);
  p.everyRow();
  for (int y : {0, 10}) {
    bool changed = false;
    for (int x = 0; x < 53; ++x) changed |= p.c.getPixel(x, y) != background;
    TEST_ASSERT_TRUE(changed);
  }
  LookingEyesEffect eyes;
  eyes.render(p.c, 0); // before the first blink: eyeballs span the canvas
  p.everyRow();
}
static void test_transition_full_height() {
  Panel p;
  Canvas from(53, 11), to(53, 11);
  from.clear(0xFF0000u);
  to.clear(0x0000FFu);
  for (int t = 1; t < static_cast<int>(Transition::Count); ++t) {
    for (float progress : {0.0f, 0.25f, 0.5f, 0.75f, 1.0f}) {
      composeTransition(p.c, from, to, static_cast<Transition>(t), progress, 1);
      p.check();
      if (progress == 0.0f || progress == 1.0f) {
        const uint32_t expected = progress == 0.0f ? 0xFF0000u : 0x0000FFu;
        for (std::size_t i = 0; i < p.c.size(); ++i)
          TEST_ASSERT_EQUAL_HEX32(expected, p.c.data()[i]);
        p.everyRow();
      }
    }
  }
}
static void test_spec_background_and_absolute_draw() {
  Panel p;
  AppSpec s;
  s.hasBackgroundColor = true;
  s.backgroundColor = 0x123456u;
  renderSpec(p.c, s, font, SpecRender{});
  for (std::size_t i = 0; i < p.c.size(); ++i)
    TEST_ASSERT_EQUAL_HEX32(0x123456u, p.c.data()[i]);
  p.everyRow();
  DrawOp op;
  op.kind = DrawKind::Pixel;
  op.color = 0xFF0000u;
  for (int y : {0, 10}) {
    op.y = y;
    s.extrasMut().draw.push_back(op);
  }
  renderSpec(p.c, s, font, SpecRender{});
  TEST_ASSERT_EQUAL_HEX32(0xFF0000u, p.c.getPixel(0, 0));
  TEST_ASSERT_EQUAL_HEX32(0xFF0000u, p.c.getPixel(0, 10));
  TEST_ASSERT_EQUAL_HEX32(0x123456u, p.c.getPixel(0, 1));
  p.check();
}
static void test_script_absolute_canvas() {
  Panel p;
  script::BerryVM vm;
  std::string error;
  TEST_ASSERT_TRUE(script::installBindings(vm, error));
  TEST_ASSERT_TRUE(vm.load("def draw() assert(height() == 11) assert(width() == 53) pixel(0, 0, 0xFF0000) pixel(width()-1, height()-1, 0x00FF00) end"));
  RenderCtx ctx;
  ctx.font = &font;
  script::BindingScope scope(&p.c, &ctx, "tall");
  TEST_ASSERT_TRUE(vm.call("draw"));
  TEST_ASSERT_EQUAL_HEX32(0xFF0000u, p.c.getPixel(0, 0));
  TEST_ASSERT_EQUAL_HEX32(0x00FF00u, p.c.getPixel(52, 10));
  p.check();
}
int main(int, char**) {
  UNITY_BEGIN();
  RUN_TEST(test_charts_and_progress);
  RUN_TEST(test_effect_overlay_and_eyes);
  RUN_TEST(test_transition_full_height);
  RUN_TEST(test_spec_background_and_absolute_draw);
  RUN_TEST(test_script_absolute_canvas);
  return UNITY_END();
}
