#include <unity.h>

#include <string>

#include "core/apps/AppRegistry.h"
#include "core/render/Canvas.h"
#include "core/script/ScriptBindings.h"
#include "core/script/ScriptHost.h"
#include "platform/linux/layout/ScriptLayouts.h"
#include "platform/linux/layout/LayoutScripting.h"
#include "platform/linux/script/ExtensionHost.h"

using namespace awtrix;

void setUp() {}
void tearDown() { script::setServices(nullptr); }

static void test_prepared_progress_draws_in_native_coordinates() {
  Canvas panel(52, 16);
  script::ScriptServices services;
  services.panel = &panel;
  auto budget = std::make_shared<layout::Budget>();
  AppRegistry apps;
  layout::LayoutScripting layouts({panel.width(),panel.height(),false}, {}, budget);
  script::ScriptHost host(apps, services, {}, {});
  script::ExtensionHost extensions(host, {&layouts});
  TEST_ASSERT_TRUE(host.set("NativeLayout", R"(
import layout
class App
  var handle
  def setup()
    self.handle = layout.prepare({'version':1,'regions':[
      {'id':'meter','box':[40,10,10,4],'progress':50,'color':0x00FF00}
    ]})
  end
  def draw()
    layout.draw(self.handle)
  end
end
return App()
)"));
  IApp* app = apps.find("NativeLayout");
  TEST_ASSERT_NOT_NULL(app);
  TEST_ASSERT_TRUE_MESSAGE(host.errorOf("NativeLayout").message.empty(),
                           host.errorOf("NativeLayout").message.c_str());
  RenderCtx ctx;
  app->render(panel, ctx);
  TEST_ASSERT_EQUAL_HEX32(0x00FF00, panel.getPixel(40, 10));
  TEST_ASSERT_EQUAL_HEX32(0x00FF00, panel.getPixel(44, 13));
  TEST_ASSERT_EQUAL_HEX32(0x202020, panel.getPixel(45, 10));
  TEST_ASSERT_EQUAL_HEX32(0, panel.getPixel(50, 10));
}

static void test_import_requires_the_platform_extension() {
  Canvas panel(32, 8);
  script::ScriptServices services;
  services.panel = &panel;
  AppRegistry apps;
  script::ScriptHost host(apps, services, {}, {});
  TEST_ASSERT_TRUE(host.set("Unavailable", "import layout\nclass App\n  def draw() end\nend\nreturn App()"));
  TEST_ASSERT_FALSE(host.errorOf("Unavailable").empty());
}

static void test_hold_ends_on_the_next_frame_without_layout_draw() {
  const uint8_t bits[] = {0xFF, 0x80};
  const FontGlyph glyphs[] = {{0, 3, 3, 4, 0, -2}};
  const GfxFont font{bits, glyphs, 'A', 'A', 4};
  const FontEntry fontEntries[] = {{"small", &font, 2, 1, 4}};
  const FontCatalog fonts{fontEntries, 1};
  Canvas panel(52, 16);
  script::ScriptServices services;
  services.panel = &panel;
  auto budget = std::make_shared<layout::Budget>();
  layout::Resources resources;
  resources.fonts = &fonts;
  AppRegistry apps;
  layout::LayoutScripting layouts({52,16,false}, resources, budget);
  script::ScriptHost host(apps, services, {}, {});
  script::ExtensionHost extensions(host, {&layouts});
  TEST_ASSERT_TRUE(host.set("Repeating", R"(
import layout
class App
  var handle, frames
  def setup()
    self.frames = 0
    self.handle = layout.prepare({'version':1,'regions':[
      {'id':'text','box':[0,0,8,8],'text':'AAAAAAAA','repeat':1}
    ]})
  end
  def draw()
    self.frames += 1
    if self.frames == 1 layout.draw(self.handle) end
  end
end
return App()
)"));
  TEST_ASSERT_TRUE_MESSAGE(host.errorOf("Repeating").empty(), host.errorOf("Repeating").message.c_str());
  RenderCtx ctx;
  apps.find("Repeating")->render(panel, ctx);
  TEST_ASSERT_TRUE(host.scrollHolds("Repeating"));
  ctx.nowMs = 25;
  apps.find("Repeating")->render(panel, ctx);
  TEST_ASSERT_FALSE(host.scrollHolds("Repeating"));
  host.remove("Repeating");
  TEST_ASSERT_EQUAL_UINT32(0, budget->used());
}

static const char* kProgress = R"({"version":1,"regions":[
  {"id":"meter","box":[0,8,12,8],"progress":50,"color":65280}]})";

static void test_updates_are_atomic_and_handles_are_owned_by_their_script() {
  Canvas panel(52, 16);
  script::ScriptServices services;
  services.panel = &panel;
  auto budget = std::make_shared<layout::Budget>();
  script::ScriptLayouts layouts({panel.width(),panel.height(),false}, {}, budget);
  std::string error;
  const int32_t id = layouts.prepare("Owner", kProgress, error);
  TEST_ASSERT_TRUE_MESSAGE(id > 0, error.c_str());
  TEST_ASSERT_FALSE(layouts.update("Owner", id,
      R"({"version":1,"regions":[{"id":"meter","box":[50,8,12,8],"progress":1}]})", error));
  bool finished = false;
  TEST_ASSERT_TRUE(layouts.draw("Owner", id, panel, {}, finished, error));
  TEST_ASSERT_EQUAL_HEX32(0x00FF00, panel.getPixel(5, 15));
  TEST_ASSERT_FALSE(layouts.draw("Other", id, panel, {}, finished, error));
  TEST_ASSERT_FALSE(layouts.release("Other", id));
  TEST_ASSERT_FALSE(layouts.update("Other", id, kProgress, error));
  TEST_ASSERT_EQUAL_UINT32(1, layouts.size());
  TEST_ASSERT_TRUE(layouts.release("Owner", id));
  TEST_ASSERT_EQUAL_UINT32(0, budget->used());
  TEST_ASSERT_FALSE(layouts.draw("Owner", id, panel, {}, finished, error));
  const int32_t replacement = layouts.prepare("Owner", kProgress, error);
  TEST_ASSERT_TRUE(replacement > id);
}

static void test_script_removal_releases_all_prepared_layouts() {
  Canvas panel(32, 16);
  script::ScriptServices services;
  services.panel = &panel;
  auto budget = std::make_shared<layout::Budget>();
  AppRegistry apps;
  layout::LayoutScripting layouts({panel.width(),panel.height(),false}, {}, budget);
  script::ScriptHost host(apps, services, {}, {});
  script::ExtensionHost extensions(host, {&layouts});
  const std::string source = R"(
class App
  var h
  def setup()
    self.h = layout.prepare({'version':1,'regions':[
      {'id':'p','box':[0,0,32,16],'progress':100}
    ]})
  end
  def draw() layout.draw(self.h) end
end
return App()
)";
  for (int i = 0; i < 12; ++i) {
    TEST_ASSERT_TRUE(host.set("Owned", source));
    TEST_ASSERT_TRUE_MESSAGE(host.errorOf("Owned").message.empty(), host.errorOf("Owned").message.c_str());
    TEST_ASSERT_TRUE(budget->used() > 0);
    host.remove("Owned");
    TEST_ASSERT_EQUAL_UINT32(0, budget->used());
  }
}

static void test_handle_limits_bound_repeated_preparations() {
  Canvas panel(32, 16);
  script::ScriptServices services;
  services.panel = &panel;
  auto budget = std::make_shared<layout::Budget>();
  script::ScriptLayouts layouts({panel.width(),panel.height(),false}, {}, budget);
  std::string error;
  for (unsigned i = 0; i < script::ScriptLayouts::kMaxPerScript; ++i)
    TEST_ASSERT_TRUE(layouts.prepare("Owner", kProgress, error) > 0);
  const auto before = budget->used();
  TEST_ASSERT_EQUAL_INT(0, layouts.prepare("Owner", kProgress, error));
  TEST_ASSERT_EQUAL_UINT32(before, budget->used());
  for (unsigned i = 0; i < script::ScriptLayouts::kMaxPerScript; ++i)
    TEST_ASSERT_TRUE(layouts.prepare("Other", kProgress, error) > 0);
  TEST_ASSERT_EQUAL_INT(0, layouts.prepare("Third", kProgress, error));
  layouts.purge("Owner");
  layouts.purge("Other");
  TEST_ASSERT_EQUAL_UINT32(0, budget->used());
}

static void test_module_replacement_and_script_errors_release_their_layouts() {
  Canvas panel(32, 16);
  script::ScriptServices services;
  services.panel = &panel;
  auto budget = std::make_shared<layout::Budget>();
  AppRegistry apps;
  layout::LayoutScripting layouts({panel.width(),panel.height(),false}, {}, budget);
  script::ScriptHost host(apps, services, {}, {});
  script::ExtensionHost extensions(host, {&layouts});
  const std::string module = R"(# @module
var m = module('prepared')
m.h = layout.prepare({'version':1,'regions':[{'id':'p','box':[0,0,32,16],'progress':50}]})
return m
)";
  std::size_t retained = 0;
  for (int i = 0; i < 8; ++i) {
    TEST_ASSERT_TRUE(host.set("prepared", module));
    TEST_ASSERT_TRUE_MESSAGE(host.errorOf("prepared").empty(), host.errorOf("prepared").message.c_str());
    if (!i) retained = budget->used();
    TEST_ASSERT_EQUAL_UINT32(retained, budget->used());
  }
  host.remove("prepared");
  TEST_ASSERT_EQUAL_UINT32(0, budget->used());
  TEST_ASSERT_TRUE(host.set("failed_module", R"(# @module
var h = layout.prepare({'version':1,'regions':[{'id':'p','box':[0,0,32,16],'progress':50}]})
raise 'value_error', 'failed module'
return module('failed_module')
)"));
  TEST_ASSERT_FALSE(host.errorOf("failed_module").empty());
  TEST_ASSERT_EQUAL_UINT32(0, budget->used());
  TEST_ASSERT_TRUE(host.set("broken", R"(
class App
  var h
  def setup()
    self.h = layout.prepare({'version':1,'regions':[{'id':'p','box':[0,0,32,16],'progress':50}]})
  end
  def draw() raise 'value_error', 'after prepare' end
end
return App()
)"));
  TEST_ASSERT_TRUE(budget->used() > 0);
  RenderCtx ctx;
  apps.find("broken")->render(panel, ctx);
  TEST_ASSERT_FALSE(host.errorOf("broken").empty());
  TEST_ASSERT_EQUAL_UINT32(0, budget->used());
  TEST_ASSERT_FALSE(host.scrollHolds("broken"));
}

int main(int, char**) {
  UNITY_BEGIN();
  RUN_TEST(test_import_requires_the_platform_extension);
  RUN_TEST(test_hold_ends_on_the_next_frame_without_layout_draw);
  RUN_TEST(test_prepared_progress_draws_in_native_coordinates);
  RUN_TEST(test_updates_are_atomic_and_handles_are_owned_by_their_script);
  RUN_TEST(test_script_removal_releases_all_prepared_layouts);
  RUN_TEST(test_handle_limits_bound_repeated_preparations);
  RUN_TEST(test_module_replacement_and_script_errors_release_their_layouts);
  return UNITY_END();
}
