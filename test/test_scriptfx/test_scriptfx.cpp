#include <unity.h>

#include <string>
#include <vector>

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
  std::string id_;
  explicit FakeOv(const char* n) : id_(n) {}
  const std::string& id() const override { return id_; }
  void render(Canvas& c, int64_t) override { c.setPixel(31, 0, 0xFF00FFu); }
};

static FakeFx g_fx("Plasma");
static FakeFx g_fxAurora("aurora");
static FakeFx g_fxMatrix("Matrix");
static FakeFx g_fxBrick("BrickBreaker");
static FakeOv g_ov("rain");
static FakeOv g_ovDrizzle("drizzle");
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
  // Added out of order and in mixed case, so the listing tests see the registry's sorting.
  g_fxReg.add(&g_fx);
  g_fxReg.add(&g_fxAurora);
  g_fxReg.add(&g_fxMatrix);
  g_fxReg.add(&g_fxBrick);
  g_ovReg.add(&g_ov);
  g_ovReg.add(&g_ovDrizzle);
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

static std::string joined(const std::vector<std::string>& names) {
  std::string out;
  for (const std::string& n : names) out += (out.empty() ? "" : ",") + n;
  return out;
}

// init() runs with no canvas, like the constructor ScriptApp calls; draw() runs with one.
static const char* kListing =
    "class Lister\n"
    "  var at_init\n"
    "  def init() self.at_init = LIST().concat(',') end\n"
    "end\n"
    "lister = Lister()\n"
    "def at_init() return lister.at_init end\n"
    "def at_draw() return LIST().concat(',') end";

static void expectListing(const char* call, const std::vector<std::string>& want) {
  registerFx();
  std::string source = kListing;
  for (std::size_t at; (at = source.find("LIST")) != std::string::npos;) source.replace(at, 4, call);
  script::BerryVM vm;
  {
    script::BindingScope s(nullptr, nullptr, "T");
    TEST_ASSERT_TRUE(load(vm, source.c_str()));
  }
  std::string atInit, atDraw;
  {
    script::BindingScope s(nullptr, nullptr, "T");
    TEST_ASSERT_TRUE(vm.callString("at_init", atInit));
  }
  Canvas c(32, 8);
  RenderCtx ctx;
  script::BindingScope s(&c, &ctx, "T");
  TEST_ASSERT_TRUE(vm.callString("at_draw", atDraw));
  TEST_ASSERT_EQUAL_STRING(joined(want).c_str(), atInit.c_str());
  TEST_ASSERT_EQUAL_STRING(joined(want).c_str(), atDraw.c_str());
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

static void test_effects_lists_registry_names_in_init_and_draw() {
  registerFx();
  TEST_ASSERT_EQUAL_STRING("aurora,BrickBreaker,Matrix,Plasma", joined(g_fxReg.names()).c_str());
  expectListing("effects", g_fxReg.names());
}

static void test_overlays_lists_registry_names_in_init_and_draw() {
  registerFx();
  TEST_ASSERT_EQUAL_STRING("drizzle,rain", joined(g_ovReg.names()).c_str());
  expectListing("overlays", g_ovReg.names());
}

static void test_every_listed_name_draws() {
  Canvas c(32, 8);
  draw("def draw()\n"
       "  var ok = effects().size() == 4 && overlays().size() == 2\n"
       "  for n : effects() ok = effect(n) && ok end\n"
       "  for n : overlays() ok = overlay(n) && ok end\n"
       "  pixel(1, 1, ok ? 0x00FF00 : 0xFF0000)\n"
       "end",
       c);
  TEST_ASSERT_EQUAL_HEX32(0x00FF00u, c.getPixel(1, 1));
}

static void test_no_registry_lists_nothing() {
  registerFx();
  const char* probe =
      "def sizes() return classname(effects()) + ':' + str(effects().size()) + ',' +"
      " classname(overlays()) + ':' + str(overlays().size()) end";
  g_svc.effects = nullptr;
  g_svc.overlays = nullptr;
  script::BerryVM vm;
  TEST_ASSERT_TRUE(load(vm, probe));
  std::string out;
  script::BindingScope s(nullptr, nullptr, "T");
  TEST_ASSERT_TRUE(vm.callString("sizes", out));
  TEST_ASSERT_EQUAL_STRING("list:0,list:0", out.c_str());

  script::setServices(nullptr);
  TEST_ASSERT_TRUE(vm.callString("sizes", out));
  TEST_ASSERT_EQUAL_STRING("list:0,list:0", out.c_str());
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
  RUN_TEST(test_effects_lists_registry_names_in_init_and_draw);
  RUN_TEST(test_overlays_lists_registry_names_in_init_and_draw);
  RUN_TEST(test_every_listed_name_draws);
  RUN_TEST(test_no_registry_lists_nothing);
  return UNITY_END();
}
