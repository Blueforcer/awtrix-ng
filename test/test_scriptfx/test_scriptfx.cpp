#include <unity.h>

#include <string>

#include "core/apps/IApp.h"
#include "core/effects/EffectRegistry.h"
#include "core/effects/IEffect.h"
#include "core/render/Canvas.h"
#include "core/script/BerryVM.h"
#include "core/script/ScriptBindings.h"
#include "core/script/ScriptServices.h"

using namespace awtrix;

struct FakeFx : IEffect {
  std::string id_;
  explicit FakeFx(const char* n) : id_(n) {}
  const std::string& id() const override { return id_; }
  void render(Canvas& c, int64_t) override {
    const uint32_t col = hasPalette() ? paletteColor(0, 0x111111u) : 0x0A0B0Cu;
    for (int y = 0; y < c.height(); ++y)
      for (int x = 0; x < c.width(); ++x) c.setPixel(x, y, col);
    if (settings().hasSpeed) c.setPixel(0, 0, 0x00FF00u);
  }
};

struct FakeOv : IEffect {
  std::string id_ = "rain";
  const std::string& id() const override { return id_; }
  void render(Canvas& c, int64_t) override { c.setPixel(31, 0, 0xFF00FFu); }
};

static FakeFx g_fx("Plasma");
static FakeOv g_ov;
static EffectRegistry g_fxReg;
static EffectRegistry g_ovReg;
static script::ScriptServices g_svc;
static long g_ms = 0;

void setUp() {
  g_ms = 0;
  g_svc = script::ScriptServices{};
  g_svc.monotonicMs = [] { return g_ms; };
  g_svc.effects = &g_fxReg;
  g_svc.overlays = &g_ovReg;
  script::setServices(&g_svc);
}
void tearDown() { script::setServices(nullptr); }

static void registerFx() {
  static bool once = false;
  if (once) return;
  once = true;
  g_fxReg.add(&g_fx);
  g_ovReg.add(&g_ov);
}

static bool load(script::BerryVM& vm, const char* user) {
  std::string err;
  if (!script::installBindings(vm, err)) {
    TEST_MESSAGE(err.c_str());
    return false;
  }
  if (!vm.load(user)) {
    TEST_MESSAGE(vm.lastError().c_str());
    return false;
  }
  return true;
}

static void draw(const char* user, Canvas& c) {
  registerFx();
  script::BerryVM vm;
  TEST_ASSERT_TRUE(load(vm, user));
  RenderCtx ctx;
  script::BindingScope s(&c, &ctx, "T");
  TEST_ASSERT_TRUE(vm.call("draw"));
}

static void test_effect_fills_background_case_insensitive() {
  Canvas c(32, 8);
  draw("def draw() effect('plasma') end", c);
  TEST_ASSERT_EQUAL_HEX32(0x0A0B0Cu, c.getPixel(5, 5));
}

static void test_unknown_effect_returns_false() {
  Canvas c(32, 8);
  draw("def draw() pixel(0, 0, effect('nope') ? 0xFF0000 : 0x00FF00) end", c);
  TEST_ASSERT_EQUAL_HEX32(0x00FF00u, c.getPixel(0, 0));
}

static void test_settings_map_is_applied() {
  Canvas c(32, 8);
  draw("def draw() effect('Plasma', {'speed': 2.0, 'palette': 'Lava', 'blend': true}) end", c);
  TEST_ASSERT_EQUAL_HEX32(0x00FF00u, c.getPixel(0, 0));
  TEST_ASSERT_TRUE(c.getPixel(5, 5) != 0x0A0B0Cu);
}

static void test_absent_settings_reset_to_defaults() {
  Canvas c(32, 8);
  draw("def draw() effect('Plasma', {'speed': 2.0}) end", c);
  Canvas c2(32, 8);
  draw("def draw() effect('Plasma') end", c2);
  TEST_ASSERT_EQUAL_HEX32(0x0A0B0Cu, c2.getPixel(0, 0));
}

static void test_palette_as_colour_list() {
  Canvas c(32, 8);
  draw("def draw() effect('Plasma', {'palette': [0xFF0000, 0x00FF00]}) end", c);
  TEST_ASSERT_EQUAL_HEX32(0xFF0000u, c.getPixel(5, 5));
}

static void test_overlay_draws_on_top_without_clearing() {
  Canvas c(32, 8);
  draw("def draw() clear(0x010101) overlay('rain') end", c);
  TEST_ASSERT_EQUAL_HEX32(0x010101u, c.getPixel(0, 0));
  TEST_ASSERT_EQUAL_HEX32(0xFF00FFu, c.getPixel(31, 0));
}

static void test_no_canvas_is_a_silent_noop() {
  registerFx();
  script::BerryVM vm;
  TEST_ASSERT_TRUE(load(vm, "def loop() effect('Plasma') overlay('rain') end"));
  script::BindingScope s(nullptr, nullptr, "T");
  TEST_ASSERT_TRUE(vm.call("loop"));
}

static void test_no_registry_returns_false() {
  g_svc.effects = nullptr;
  registerFx();
  script::BerryVM vm;
  TEST_ASSERT_TRUE(load(vm, "def draw() pixel(0, 0, effect('Plasma') ? 0xFF0000 : 0x00FF00) end"));
  Canvas c(32, 8);
  RenderCtx ctx;
  script::BindingScope s(&c, &ctx, "T");
  TEST_ASSERT_TRUE(vm.call("draw"));
  TEST_ASSERT_EQUAL_HEX32(0x00FF00u, c.getPixel(0, 0));
}

int main(int, char**) {
  UNITY_BEGIN();
  RUN_TEST(test_effect_fills_background_case_insensitive);
  RUN_TEST(test_unknown_effect_returns_false);
  RUN_TEST(test_settings_map_is_applied);
  RUN_TEST(test_absent_settings_reset_to_defaults);
  RUN_TEST(test_palette_as_colour_list);
  RUN_TEST(test_overlay_draws_on_top_without_clearing);
  RUN_TEST(test_no_canvas_is_a_silent_noop);
  RUN_TEST(test_no_registry_returns_false);
  return UNITY_END();
}
