#include <unity.h>
#include <algorithm>
#include <climits>
#include <memory>

#include "core/CoreEngine.h"
#include "core/apps/SpecRenderer.h"
#include "platform/linux/layout/LayoutJson.h"
#include "platform/linux/layout/LayoutPayload.h"
#include "core/payload/PayloadParser.h"
#include "core/render/RenderPipeline.h"
#include "platform/linux/render/PageZoom.h"

using namespace awtrix;
using namespace awtrix::layout;

namespace {
const uint8_t bits[] = {0xFF, 0x80};
const FontGlyph glyphs[] = {{0, 3, 3, 4, 0, -2}};
const GfxFont font{bits, glyphs, 'A', 'A', 4};
const FontEntry fontEntries[] = {{"small", &font, 2, 1, 4}};
const FontCatalog fonts{fontEntries, 1};
int opened = 0;
bool imageOk = true;
std::size_t imageCharge = 1024;
int imageWidth = 12;
int infoWidth = 0, infoHeight = 0, openWidth = 0, openHeight = 0;
int storageAllow = -1, storageLive = 0;
void* faultAllocate(std::size_t bytes) {
  if (storageAllow == 0) return nullptr;
  if (storageAllow > 0) --storageAllow;
  void* memory = std::malloc(bytes);
  if (memory) ++storageLive;
  return memory;
}
void faultRelease(void* memory) { if (memory) --storageLive; std::free(memory); }
struct FakeImage : IPageIcon {
  int width_ = 12;
  IconLoad begin(const std::string&, int, int) override { return IconLoad::kMissing; }
  IconLoad beginNative(std::string_view, int w, int h) override {
    ++opened; openWidth = w; openHeight = h; width_ = imageWidth;
    return imageOk ? IconLoad::kGood : IconLoad::kMissing;
  }
  void clear() override {}
  int width() const override { return width_; }
  int height() const override { return 8; }
  void advance(int64_t) override {}
  void blit(Canvas& c, int x, int y) const override { c.fillRect(x, y, width_, 8, 0x123456); }
  std::unique_ptr<IPageIcon> create() const override { return std::make_unique<FakeImage>(); }
  bool nativeInfo(std::string_view id, int w, int h, int& iw, int& ih, std::size_t& bytes) const override {
    iw = imageWidth; ih = 8; bytes = imageCharge;
    infoWidth = w; infoHeight = h;
    return id != "missing" && w >= iw && h >= ih;
  }
};
FakeImage icons;
Resources resources() {
  Resources r;
  r.fonts = &fonts;
  r.icons = &icons;
  return r;
}
std::unique_ptr<LayoutPayload> payloadExtension;
LayoutSpec read(const char* json) {
  LayoutSpec spec;
  DispatchDetail error;
  TEST_ASSERT_TRUE_MESSAGE(layout::parse(api::JsonReader(json), spec, error), error.message.c_str());
  return spec;
}
std::unique_ptr<PreparedLayout> make(const LayoutSpec& spec, int w = 52, int h = 16,
                                    std::shared_ptr<Budget> budget = std::make_shared<Budget>()) {
  DispatchDetail error;
  auto p = PreparedLayout::prepare(spec, {w, h, false}, resources(), budget, error);
  TEST_ASSERT_NOT_NULL_MESSAGE(p.get(), error.message.c_str());
  return p;
}
Region textRegion(const char* id, Box box, const char* text) {
  Region r; r.id = id; r.box = box; r.text = text;
  return r;
}
struct Display : IDisplayService { void sendScreen() override {} };
struct System : ISystemService {
  void reboot() override {} void sleep(uint64_t) override {}
  void factoryReset() override {} void resetSettings() override {}
};
struct Clock : IPageClock { void fill(RenderCtx& ctx, int64_t now) override { ctx.nowMs = now; } };
}

void setUp() {
  opened = 0; imageOk = true; imageCharge = 1024; imageWidth = 12;
  payloadExtension = std::make_unique<LayoutPayload>(DisplayProfile{52,16,false}, resources(), std::make_shared<Budget>());
  payload::setKeyHandlers(payloadExtension->handlers());
}
void tearDown() { payload::setKeyHandlers(nullptr); payloadExtension.reset(); }

static void test_parser_is_strict_atomic_and_does_not_change_legacy_payloads() {
  const char* invalid[] = {
    R"({"version":2,"regions":[]})",
    R"({"version":1,"regions":[],"slots":{}})",
    R"({"version":1,"preset":"single","slots":{"main":{"text":"A"}}})",
    R"({"version":1,"regions":[{"id":"a","box":[0,0,8,8],"text":"A","progress":3}]})",
    R"({"version":1,"regions":[{"id":"a","box":[0,0,8,8],"text":"A","font":"small","font":"small"}]})",
    R"({"version":1,"regions":[{"id":"a","box":[0,0,2147483647,8],"text":"A"}]})"
    ,R"({"version":1,"regions":[{"id":"a","box":[0,0,8,8],"text":"A","scroll":{"speed":4294967396}}]})"
    ,R"({"version":1,"regions":[{"id":"a","box":[0,0,8,8],"text":"A","scroll":{"speed":100,"speed":200}}]})"
  };
  for (const char* json : invalid) {
    LayoutSpec unchanged; unchanged.background = 0xABCDEF;
    DispatchDetail error;
    TEST_ASSERT_FALSE(layout::parse(api::JsonReader(json), unchanged, error));
    TEST_ASSERT_EQUAL_HEX32(0xABCDEF, unchanged.background);
    TEST_ASSERT_FALSE(error.message.empty());
  }
  AppSpec app;
  DispatchDetail error;
  TEST_ASSERT_TRUE(payload::parse(R"({"text":"A","textOffsetX":-2,"font":"large"})", false, app));
  TEST_ASSERT_EQUAL_INT(-2, app.textOffsetX);
  TEST_ASSERT_NULL(app.extras().content.get());
  TEST_ASSERT_FALSE(payload::parse(R"({"text":"A","layout":{"version":1,"regions":[{"id":"main","box":[0,0,32,8],"text":"A"}]}})", false, app, nullptr, nullptr, &error));
  TEST_ASSERT_TRUE(payload::parse(R"({"repeat":2,"layout":{"version":1,"regions":[{"id":"main","box":[0,0,32,8],"text":"A"}]}})", false, app));
  TEST_ASSERT_NOT_NULL(app.extras().content.get());
}

static void test_layout_payload_requires_a_taller_registered_display() {
  const char* json = R"({"layout":{"version":1,"regions":[{"id":"main","box":[0,0,8,8],"text":"A"}]}})";
  for (const int height : {8, 9, 16}) {
    LayoutPayload extension({32,height,false}, resources(), std::make_shared<Budget>());
    payload::setKeyHandlers(extension.handlers());
    AppSpec spec;
    DispatchDetail error;
    const bool accepted = payload::parse(json, false, spec, nullptr, nullptr, &error);
    TEST_ASSERT_EQUAL(height > 8, accepted);
    TEST_ASSERT_EQUAL(height > 8, bool(spec.extras().content));
    if (!accepted) TEST_ASSERT_EQUAL_STRING("layout", error.field.c_str());
  }
  payload::setKeyHandlers(payloadExtension->handlers());
}

static void test_free_regions_clip_original_fonts_and_restore_parent_clip() {
  LayoutSpec spec;
  spec.regions.push_back(textRegion("a", {3,4,8,1}, "AAA"));
  auto page = make(spec);
  Canvas c(52,16);
  c.setClipRect(2,2,20,12);
  const auto clip = c.clipRect();
  page->draw(c, {});
  int lit = 0;
  for (int y = 0; y < 16; ++y) for (int x = 0; x < 52; ++x) if (c.getPixel(x,y)) {
    ++lit; TEST_ASSERT_EQUAL_INT(4,y); TEST_ASSERT_TRUE(x >= 3 && x < 11);
  }
  TEST_ASSERT_GREATER_THAN_INT(0, lit);
  TEST_ASSERT_EQUAL_INT(clip.left, c.clipRect().left);
  TEST_ASSERT_EQUAL_INT(clip.top, c.clipRect().top);
  Canvas copy = c;
  copy.setPixel(3,15,0xFF);
  TEST_ASSERT_EQUAL_HEX32(0, copy.getPixel(3,15));
}

static void test_duplicate_ids_unknown_fonts_and_out_of_bounds_boxes_reject() {
  LayoutSpec spec;
  spec.regions.push_back(textRegion("a", {0,0,52,8}, "A"));
  spec.regions.push_back(spec.regions.front());
  DispatchDetail error;
  auto budget = std::make_shared<Budget>();
  TEST_ASSERT_NULL(PreparedLayout::prepare(spec, {52,16,false}, resources(), budget, error).get());
  spec.regions.pop_back();
  spec.regions[0].font = "unavailable";
  TEST_ASSERT_NULL(PreparedLayout::prepare(spec, {52,16,false}, resources(), budget, error).get());
  spec.regions[0].font = "small";
  spec.regions[0].box.x = INT_MAX;
  TEST_ASSERT_NULL(PreparedLayout::prepare(spec, {52,16,false}, resources(), budget, error).get());
  TEST_ASSERT_EQUAL_UINT(0, budget->used());
}

static void test_native_motion_is_independent_of_frame_count_and_aggregates_repeat() {
  LayoutSpec spec;
  for (int i = 0; i < 2; ++i) {
    Region r = textRegion(i ? "slow" : "fast", {i * 16,0,16,8}, i ? "AAAAAAAA" : "AA");
    r.scroll.hasMode = true; r.scroll.mode = ScrollMode::Wrap;
    r.scroll.hasWhenFits = true; r.scroll.whenFits = ScrollWhenFits::Scroll;
    r.scroll.hasEntry = true; r.scroll.entry = ScrollEntry::Offscreen;
    r.scroll.hasHoldMs = true; r.scroll.holdMs = 0;
    r.scroll.hasSpeed = true; r.scroll.speed = 480; // 100 pixels/second
    r.repeat = 1;
    spec.regions.push_back(r);
  }
  auto sparse = make(spec,32,8), dense = make(spec,32,8);
  Canvas a(32,8), b(32,8);
  FrameContext frame;
  sparse->draw(a,frame); dense->draw(b,frame);
  for (int now = 10; now <= 300; now += 10) { frame.nowMs = now; dense->draw(b,frame); }
  const auto half = sparse->draw(a,frame);
  TEST_ASSERT_EQUAL_MEMORY(a.data(),b.data(),a.size()*4);
  TEST_ASSERT_TRUE(half.wantsMoreTime);
  TEST_ASSERT_FALSE(half.passesDone);
  frame.nowMs = 600;
  const auto done = sparse->draw(a,frame);
  TEST_ASSERT_FALSE(done.wantsMoreTime);
  TEST_ASSERT_TRUE(done.passesDone);
  spec.regions[0].scroll.mode = spec.regions[1].scroll.mode = ScrollMode::Static;
  DispatchDetail error;
  TEST_ASSERT_TRUE(sparse->update(spec,error));
  TEST_ASSERT_FALSE(sparse->draw(a,frame).passesDone);
}

static void test_reorder_keeps_animation_by_id_and_failed_updates_keep_frame() {
  LayoutSpec spec;
  Region r = textRegion("a", {0,0,16,8}, "AAAAAAAA");
  r.scroll.hasHoldMs = true; r.scroll.holdMs = 0;
  spec.regions.push_back(r);
  r.id = "b"; r.box.y = 8; spec.regions.push_back(r);
  auto page = make(spec);
  Canvas before(52,16), after(52,16);
  FrameContext frame;
  page->draw(before,frame);
  frame.nowMs = 500;
  page->draw(before,frame);
  std::reverse(spec.regions.begin(),spec.regions.end());
  DispatchDetail error;
  TEST_ASSERT_TRUE(page->update(spec,error));
  page->draw(after,frame);
  TEST_ASSERT_EQUAL_MEMORY(before.data(),after.data(),before.size()*4);
  const uint64_t revision = page->revision();
  spec.regions[0].box.width = 1000;
  TEST_ASSERT_FALSE(page->update(spec,error));
  TEST_ASSERT_EQUAL_UINT64(revision,page->revision());
  page->draw(after,frame);
  TEST_ASSERT_EQUAL_MEMORY(before.data(),after.data(),before.size()*4);
}

static void test_untrusted_global_scroll_limits_are_bounded_without_changing_settings() {
  LayoutSpec spec;
  Region r=textRegion("a",{0,0,32,8},"AAAAAAAAAAAAAAAA");
  r.scroll.hasMode=true; r.scroll.mode=ScrollMode::Loop;
  spec.regions.push_back(r);
  auto huge=make(spec,32,8), bounded=make(spec,32,8);
  Canvas a(32,8), b(32,8);
  FrameContext unsafe, safe;
  unsafe.scrollDefaults.gap=INT_MAX;
  unsafe.scrollDefaults.speed=INT_MAX;
  unsafe.scrollDefaults.holdMs=INT_MIN;
  safe.scrollDefaults.gap=32767;
  safe.scrollDefaults.speed=1000000;
  safe.scrollDefaults.holdMs=0;
  huge->draw(a,unsafe); bounded->draw(b,safe);
  unsafe.nowMs=safe.nowMs=1000000000;
  huge->draw(a,unsafe); bounded->draw(b,safe);
  TEST_ASSERT_EQUAL_MEMORY(a.data(),b.data(),a.size()*4);
  TEST_ASSERT_EQUAL_INT(INT_MAX,unsafe.scrollDefaults.gap);
  unsafe.scrollDefaults.gap=INT_MIN;
  unsafe.scrollDefaults.speed=INT_MIN;
  huge->draw(a,unsafe);
}

static void test_shared_budget_rejects_peak_and_returns_charge_after_release() {
  LayoutSpec spec; spec.regions.push_back(textRegion("a",{0,0,32,8},"A"));
  auto probeBudget = std::make_shared<Budget>();
  auto probe = make(spec,32,8,probeBudget);
  Limits limits; limits.preparedBytes = probeBudget->used();
  auto budget = std::make_shared<Budget>(limits);
  auto page = make(spec,32,8,budget);
  DispatchDetail error;
  TEST_ASSERT_NULL(PreparedLayout::prepare(spec,{32,8,false},resources(),budget,error).get());
  TEST_ASSERT_FALSE(page->update(spec,error));
  TEST_ASSERT_EQUAL_UINT(limits.preparedBytes,budget->used());
  page.reset();
  TEST_ASSERT_EQUAL_UINT(0,budget->used());
}

static void test_chart_points_and_progress_use_region_geometry() {
  auto spec = read(R"({"version":1,"regions":[{"id":"p","box":[4,10,20,3],"progress":50,"color":"#00FF00","trackColor":"#0000FF"},{"id":"c","box":[0,0,52,8],"chart":{"type":"line","values":[0,100],"min":0,"max":100},"color":"#FF0000"}]})");
  spec.regions[1].values.resize(128,50);
  auto page = make(spec);
  Canvas c(52,16); page->draw(c,{});
  TEST_ASSERT_EQUAL_HEX32(0x00FF00,c.getPixel(13,12));
  TEST_ASSERT_EQUAL_HEX32(0x0000FF,c.getPixel(14,12));
  TEST_ASSERT_EQUAL_HEX32(0,c.getPixel(24,12));
  spec.regions[1].values.push_back(50);
  DispatchDetail error;
  TEST_ASSERT_FALSE(page->update(spec,error));
}

static void test_images_fully_decode_before_admission_and_failed_reload_keeps_old_pixels() {
  auto spec = read(R"({"version":1,"regions":[{"id":"icon","box":[0,0,12,16],"icon":"weather"},{"id":"main","box":[13,0,39,16],"text":"A"}]})");
  auto budget = std::make_shared<Budget>();
  auto page = make(spec,52,16,budget);
  TEST_ASSERT_EQUAL_INT(1,opened);
  const auto originalCharge = budget->used();
  Canvas c(52,16); page->draw(c,{});
  TEST_ASSERT_EQUAL_INT(1,opened);
  TEST_ASSERT_EQUAL_HEX32(0x123456,c.getPixel(0,4));
  imageOk = false; imageCharge = 2048;
  DispatchDetail updateError;
  spec.regions[0].asset = "replacement";
  TEST_ASSERT_FALSE(page->update(spec,updateError));
  spec.regions[0].asset = "weather";
  TEST_ASSERT_EQUAL_UINT(originalCharge,budget->used());
  page->invalidateAssets(); page->draw(c,{});
  TEST_ASSERT_EQUAL_INT(3,opened);
  TEST_ASSERT_EQUAL_UINT(originalCharge,budget->used());
  TEST_ASSERT_EQUAL_HEX32(0x123456,c.getPixel(0,4));
  imageOk = true;
  page->invalidateAssets(); page->draw(c,{});
  TEST_ASSERT_EQUAL_UINT(originalCharge+1024,budget->used());
  page.reset(); TEST_ASSERT_EQUAL_UINT(0,budget->used());
  imageOk = false;
  DispatchDetail error;
  TEST_ASSERT_NULL(PreparedLayout::prepare(spec,{52,16,false},resources(),budget,error).get());
  TEST_ASSERT_EQUAL_UINT(0,budget->used());
}

static void test_image_updates_reuse_decode_and_keep_old_page_on_other_region_failure() {
  auto spec = read(R"({"version":1,"regions":[{"id":"icon","box":[0,0,12,16],"icon":"weather"},{"id":"main","box":[13,0,39,16],"text":"A"}]})");
  imageCharge = 40000;
  auto budget = std::make_shared<Budget>();
  auto page = make(spec,52,16,budget);
  auto charge = budget->used();
  TEST_ASSERT_GREATER_THAN_UINT(budget->limits().preparedBytes / 2, charge);
  DispatchDetail error;
  spec.regions[1].color = 0xFF0000;
  spec.regions[1].hasColor = true;
  TEST_ASSERT_TRUE_MESSAGE(page->update(spec,error), error.message.c_str());
  TEST_ASSERT_EQUAL_INT(1,opened);
  TEST_ASSERT_EQUAL_UINT(charge,budget->used());
  // Reload a smaller asset, then fail another decoder after selecting it for reuse.
  imageCharge = 1024;
  Canvas c(52,16);
  page->invalidateAssets();
  page->draw(c,{});
  charge = budget->used();
  TEST_ASSERT_EQUAL_INT(2,opened);
  Region second = spec.regions[0];
  second.id = "other";
  second.asset = "replacement";
  spec.regions.push_back(std::move(second));
  imageOk = false;
  TEST_ASSERT_FALSE(page->update(spec,error));
  TEST_ASSERT_EQUAL_INT(3,opened);
  page->draw(c,{});
  TEST_ASSERT_EQUAL_HEX32(0x123456,c.getPixel(0,4));
  TEST_ASSERT_EQUAL_UINT(charge,budget->used());
  page.reset();
  TEST_ASSERT_EQUAL_UINT(0,budget->used());
}

static void test_pipeline_notifications_and_admission_are_native_and_atomic() {
  sound::AudioRouter audio;
  Display display; System system; Clock clock;
  CoreEngine engine(audio,display,system);
  AppRegistry apps; EffectRegistry effects, overlays;
  RenderPipelineDeps deps;
  deps.engine=&engine; deps.apps=&apps; deps.effects=&effects; deps.overlays=&overlays;
  deps.fonts=&fonts; deps.audio=&audio; deps.clock=&clock;
  RenderPipeline pipeline(52,16,deps);
  DispatchDetail error;
  const char* payload = R"({"hold":true,"layout":{"version":1,"regions":[{"id":"p","box":[0,8,52,8],"progress":100,"color":"#ABCDEF"}]}})";
  TEST_ASSERT_EQUAL_INT(DispatchResult::Ok,engine.notify(payload,0,error));
  Canvas c(52,16); pipeline.renderFrame(c,0);
  TEST_ASSERT_EQUAL_HEX32(0xABCDEF,c.getPixel(51,15));
  const auto generation=engine.notifications().generation();
  TEST_ASSERT_EQUAL_INT(DispatchResult::ValidationError,engine.notify(R"({"stack":false,"layout":{"version":1,"regions":[{"id":"x","box":[0,0,999,8],"text":"A"}]}})",0,error));
  TEST_ASSERT_EQUAL_UINT(generation,engine.notifications().generation());
}

static void test_enlarged_pages_leave_a_layout_at_panel_size() {
  sound::AudioRouter audio;
  Display display; System system; Clock clock;
  CoreEngine engine(audio,display,system);
  AppRegistry apps; EffectRegistry effects, overlays;
  PageZoom zoom(52,16,icons);
  RenderPipelineDeps deps;
  deps.engine=&engine; deps.apps=&apps; deps.effects=&effects; deps.overlays=&overlays;
  deps.fonts=&fonts; deps.audio=&audio; deps.clock=&clock; deps.zoom=&zoom;
  RenderPipeline pipeline(52,16,deps);
  DispatchDetail error;
  const char* payload = R"({"hold":true,"layout":{"version":1,"regions":[{"id":"p","box":[51,15,1,1],"progress":100,"color":"#ABCDEF"}]}})";
  TEST_ASSERT_EQUAL_INT(DispatchResult::Ok,engine.notify(payload,0,error));
  Canvas c(52,16); pipeline.renderFrame(c,0);
  TEST_ASSERT_EQUAL_HEX32(0xABCDEF,c.getPixel(51,15));
  TEST_ASSERT_EQUAL_HEX32(0,c.getPixel(50,14));
}

static void test_asset_dimension_change_reprepares_atomically() {
  auto spec=read(R"({"version":1,"regions":[{"id":"icon","box":[0,0,12,16],"icon":"weather"},{"id":"main","box":[13,0,39,16],"text":"A"}]})");
  auto page=make(spec);
  Canvas c(52,16);
  page->draw(c,{});
  TEST_ASSERT_EQUAL_HEX32(0x123456,c.getPixel(9,4));
  imageWidth=6;
  page->invalidateAssets();
  page->draw(c,{});
  TEST_ASSERT_EQUAL_HEX32(0,c.getPixel(9,4));
  TEST_ASSERT_EQUAL_HEX32(0x123456,c.getPixel(3,4));
  const auto revision=page->revision();
  imageWidth=53; // wider than the display
  page->invalidateAssets(); page->draw(c,{});
  TEST_ASSERT_EQUAL_UINT64(revision,page->revision());
  TEST_ASSERT_EQUAL_HEX32(0x123456,c.getPixel(3,4));
}

static void test_stale_scroll_completion_cannot_end_updated_layout() {
  sound::AudioRouter audio;
  Display display; System system;
  CoreEngine engine(audio,display,system);
  engine.setFontCatalog(&fonts);
  DispatchDetail error;
  const char* payload=R"({"layout":{"version":1,"regions":[{"id":"main","box":[0,0,32,8],"text":"A"}]}})";
  TEST_ASSERT_EQUAL_INT(DispatchResult::Ok,engine.setPushedApp("native",payload,error));
  TEST_ASSERT_EQUAL_INT(DispatchResult::Ok,engine.setPushedApp("other",R"({"text":"A"})",error));
  engine.appHost().setApps({"native","other"});
  engine.state().settings().autoTransition=true;
  engine.state().settings().appDurationMs=10000;
  engine.tick(0);
  const auto old=engine.pushedApp("native")->extras().content->revision();
  TEST_ASSERT_EQUAL_INT(DispatchResult::Ok,engine.setPushedApp("native",payload,error));
  engine.appHost().setApps({"native","other"});
  engine.setRotationPassesDone("native",true,old);
  engine.tick(20);
  TEST_ASSERT_FALSE(engine.appHost().inTransition());
}

static void test_settings_and_assets_clear_completion_before_engine_tick() {
  for (bool assetEvent : {false, true}) {
    sound::AudioRouter audio;
    Display display; System system;
    CoreEngine engine(audio,display,system);
    engine.setFontCatalog(&fonts);
    DispatchDetail error;
    const char* payload=R"({"repeat":1,"layout":{"version":1,"regions":[{"id":"main","box":[0,0,32,8],"text":"AAAAAAAAAAAAAAAA"}]}})";
    TEST_ASSERT_EQUAL_INT(DispatchResult::Ok,engine.setPushedApp("native",payload,error));
    TEST_ASSERT_EQUAL_INT(DispatchResult::Ok,engine.setPushedApp("other",R"({"text":"A"})",error));
    TEST_ASSERT_EQUAL_INT(DispatchResult::Ok,engine.notify(payload,0,error));
    engine.appHost().setApps({"native","other"});
    engine.state().settings().autoTransition=true;
    engine.tick(0);
    engine.setRotationPassesDone("native",true,engine.pushedApp("native")->extras().content->revision());
    engine.setNotificationPassesDone(engine.notifications().generation(),true);
    if (assetEvent) engine.invalidateContentAssets();
    else {
      Command command(CommandType::SetSettings);
      command.payload=R"({"scroll":{"speed":200}})";
      TEST_ASSERT_EQUAL_INT(DispatchResult::Ok,engine.execute(command));
    }
    engine.tick(20);
    TEST_ASSERT_FALSE(engine.appHost().inTransition());
    TEST_ASSERT_TRUE(engine.hasNotification());
  }
}

static void test_asset_invalidation_reaches_hidden_and_queued_layouts() {
  sound::AudioRouter audio;
  Display display; System system;
  CoreEngine engine(audio,display,system);
  engine.setFontCatalog(&fonts);
  DispatchDetail error;
  const char* payload=R"({"layout":{"version":1,"regions":[{"id":"icon","box":[0,0,12,16],"icon":"weather"},{"id":"main","box":[13,0,39,16],"text":"A"}]}})";
  TEST_ASSERT_EQUAL_INT(DispatchResult::Ok,engine.setPushedApp("a",payload,error));
  TEST_ASSERT_EQUAL_INT(DispatchResult::Ok,engine.setPushedApp("b",payload,error));
  TEST_ASSERT_EQUAL_INT(DispatchResult::Ok,engine.notify(payload,0,error));
  TEST_ASSERT_EQUAL_INT(DispatchResult::Ok,engine.notify(R"({"stack":true,"layout":{"version":1,"regions":[{"id":"icon","box":[0,0,12,16],"icon":"weather"},{"id":"main","box":[13,0,39,16],"text":"A"}]}})",0,error));
  Canvas c(52,16);
  imageWidth=6;
  engine.invalidateContentAssets();
  for (const char* id : {"a","b"}) {
    engine.pushedApp(id)->extras().content->draw(c,{});
    TEST_ASSERT_EQUAL_HEX32(0,c.getPixel(9,4));
  }
  engine.notifications().dismiss(0);
  engine.notifications().current().extras().content->draw(c,{});
  TEST_ASSERT_EQUAL_HEX32(0,c.getPixel(9,4));
}

static void test_payload_admission_failure_keeps_the_published_content() {
  sound::AudioRouter audio;
  Display display; System system;
  auto engine = std::make_unique<CoreEngine>(audio, display, system);
  DispatchDetail error;
  const char* original = R"({"layout":{"version":1,"regions":[{"id":"p","box":[0,0,52,16],"progress":100,"color":"#123456"}]}})";
  TEST_ASSERT_EQUAL_INT(DispatchResult::Ok, engine->setPushedApp("native", original, error));
  const auto revision = engine->pushedApp("native")->extras().content->revision();
  const char* next = R"({"layout":{"version":1,"regions":[{"id":"replacement_region_with_checked_storage","box":[0,0,52,16],"progress":50,"color":"#ABCDEF"}]}})";
  Canvas canvas(52,16);
  bool admitted = false;
  for (int stage = 0; stage < 64 && !admitted; ++stage) {
    storageAllow = stage;
    storage::setAllocator({faultAllocate, faultRelease});
    const auto result = engine->setPushedApp("native", next, error);
    storage::setAllocator({});
    admitted = result == DispatchResult::Ok;
    if (!admitted) {
      TEST_ASSERT_EQUAL_INT(DispatchResult::ValidationError, result);
      TEST_ASSERT_EQUAL_INT(0, storageLive);
      const auto& content = engine->pushedApp("native")->extras().content;
      TEST_ASSERT_EQUAL_UINT64(revision, content->revision());
      content->draw(canvas, {});
      TEST_ASSERT_EQUAL_HEX32(0x123456, canvas.getPixel(51,15));
    }
  }
  TEST_ASSERT_TRUE(admitted);
  engine.reset();
  TEST_ASSERT_EQUAL_INT(0, storageLive);
}

static void test_storage_failure_rejects_parse_and_update_without_partial_replacement() {
  const char* json = R"({"version":1,"regions":[{"id":"main","box":[0,0,32,8],"text":"AAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAA"},{"id":"chart","box":[0,0,32,8],"chart":{"values":[1,2,3,4,5,6,7,8]}}]})";
  bool parsed = false;
  for (int stage = 0; stage < 40 && !parsed; ++stage) {
    LayoutSpec output; output.background = 0xABCDEF;
    DispatchDetail error;
    storageAllow = stage;
    storage::setAllocator({faultAllocate,faultRelease});
    parsed = layout::parse(api::JsonReader(json),output,error);
    storage::setAllocator({});
    if (!parsed) {
      TEST_ASSERT_EQUAL_HEX32(0xABCDEF,output.background);
      TEST_ASSERT_EQUAL_STRING("out of memory",error.message.c_str());
      TEST_ASSERT_EQUAL_INT(0,storageLive);
    }
  }
  TEST_ASSERT_TRUE(parsed);
  TEST_ASSERT_EQUAL_INT(0,storageLive);

  auto original = read(R"({"version":1,"regions":[{"id":"p","box":[0,0,32,8],"progress":100,"color":"#123456"}]})");
  auto next = read(json);
  auto budget = std::make_shared<Budget>();
  auto page = make(original,32,8,budget);
  const auto charge = budget->used(), revision = page->revision();
  Canvas canvas(32,8);
  bool updated = false;
  for (int stage = 0; stage < 40 && !updated; ++stage) {
    DispatchDetail error;
    storageAllow = stage;
    storage::setAllocator({faultAllocate,faultRelease});
    updated = page->update(next,error);
    storage::setAllocator({});
    if (!updated) {
      TEST_ASSERT_EQUAL_STRING("out of memory",error.message.c_str());
      TEST_ASSERT_EQUAL_UINT64(revision,page->revision());
      TEST_ASSERT_EQUAL_UINT(charge,budget->used());
      TEST_ASSERT_EQUAL_INT(0,storageLive);
      page->draw(canvas,{});
      TEST_ASSERT_EQUAL_HEX32(0x123456,canvas.getPixel(31,7));
    }
  }
  TEST_ASSERT_TRUE(updated);
  page.reset();
  TEST_ASSERT_EQUAL_INT(0,storageLive);
  TEST_ASSERT_EQUAL_UINT(0,budget->used());
}

static void test_checked_string_decode_keeps_unicode_and_rejects_unbounded_options() {
  auto spec = read(R"({"version":1,"regions":[{"id":"a","box":[0,0,32,8],"text":"A\n\u00e4\ud83d\ude00","scroll":{"mode":"lo\u006fp"},"color":"#FF0000"}]})");
  TEST_ASSERT_EQUAL_STRING("A\n\xc3\xa4\xf0\x9f\x98\x80",spec.regions[0].text.c_str());
  TEST_ASSERT_EQUAL_INT(ScrollMode::Loop,spec.regions[0].scroll.mode);
  auto unprefixed = read(R"({"version":1,"regions":[{"id":"p","box":[0,0,32,8],"progress":100,"color":"F00"}]})");
  TEST_ASSERT_EQUAL_HEX32(0xFF0000,unprefixed.regions[0].color);
  for (const char* option : {"scroll", "color"}) {
    std::string json = "{\"version\":1,\"regions\":[{\"id\":\"a\",\"box\":[0,0,32,8],\"text\":\"A\",\"";
    json += option; json += "\":\""; json.append(9000,'x'); json += "\"}]}";
    DispatchDetail error; LayoutSpec out;
    storageAllow = 0; storage::setAllocator({faultAllocate,faultRelease});
    const bool ok = layout::parse(api::JsonReader(json),out,error);
    storage::setAllocator({});
    TEST_ASSERT_FALSE(ok);
    TEST_ASSERT_EQUAL_INT(0,storageLive);
  }
}

static void test_every_string_allocation_failure_keeps_the_short_oom_diagnostic() {
  const std::string longValue(48,'A');
  std::vector<std::string> payloads;
  for (const char* field : {"id","text","icon","font"}) {
    std::string json = "{\"version\":1,\"regions\":[{\"box\":[0,0,32,8],\"";
    json += field; json += "\":\""; json += longValue; json += "\"}]}";
    payloads.push_back(std::move(json));
  }
  for (const auto& json : payloads) {
    LayoutSpec output; output.background = 0xABCDEF;
    DispatchDetail error;
    storageAllow = 0;
    storage::setAllocator({faultAllocate,faultRelease});
    const bool parsed = layout::parse(api::JsonReader(json),output,error);
    storage::setAllocator({});
    TEST_ASSERT_FALSE(parsed);
    TEST_ASSERT_EQUAL_STRING("layout",error.field.c_str());
    TEST_ASSERT_EQUAL_STRING("out of memory",error.message.c_str());
    TEST_ASSERT_EQUAL_HEX32(0xABCDEF,output.background);
    TEST_ASSERT_EQUAL_INT(0,storageLive);
  }
}

static void test_image_regions_refuse_icon_text_they_cannot_show() {
  auto spec = read(R"({"version":1,"regions":[{"id":"a","box":[0,0,8,8],"icon":"data:image/gif;base64,R0lGODlh"}]})");
  TEST_ASSERT_EQUAL_STRING("data:image/gif;base64,R0lGODlh", std::string(spec.regions[0].asset.view()).c_str());
  const std::string raw(80,'A');
  const std::string invalid[] = {
      R"({"version":1,"regions":[{"id":"a","box":[0,0,8,8],"icon":")" + raw + R"("}]})",
      R"({"version":1,"regions":[{"id":"a","box":[0,0,8,8],"icon":"data:image/png;base64,iVBORw0KGgo="}]})",
      R"({"version":1,"regions":[{"id":"a","box":[0,0,8,8],"icon":""}]})"};
  for (const auto& json : invalid) {
    LayoutSpec out;
    DispatchDetail error;
    TEST_ASSERT_FALSE_MESSAGE(layout::parse(api::JsonReader(json),out,error), json.c_str());
    TEST_ASSERT_TRUE(error.field.find(".icon") != std::string::npos);
    TEST_ASSERT_FALSE(error.message.empty());
  }
}

static void test_url_pictures_are_fitted_to_their_region_other_images_to_the_display() {
  auto remote = read(R"({"version":1,"regions":[{"id":"a","box":[3,2,20,10],"icon":"https://example.com/c.jpg"}]})");
  TEST_ASSERT_EQUAL_STRING("https://example.com/c.jpg", std::string(remote.regions[0].asset.view()).c_str());
  make(remote);
  TEST_ASSERT_EQUAL_INT(20, infoWidth);
  TEST_ASSERT_EQUAL_INT(10, infoHeight);
  TEST_ASSERT_EQUAL_INT(20, openWidth);
  TEST_ASSERT_EQUAL_INT(10, openHeight);
  make(read(R"({"version":1,"regions":[{"id":"a","box":[3,2,20,10],"icon":"sun"}]})"));
  TEST_ASSERT_EQUAL_INT(52, infoWidth);
  TEST_ASSERT_EQUAL_INT(16, infoHeight);
  TEST_ASSERT_EQUAL_INT(52, openWidth);
  TEST_ASSERT_EQUAL_INT(16, openHeight);
}

namespace {
struct FillEffect : IEffect {
  std::string name = "fill";
  int renders = 0;
  bool sawPalette = false;
  const std::string& id() const override { return name; }
  void render(Canvas& c, int64_t) override {
    ++renders;
    sawPalette = hasPalette();
    c.clear(paletteColor(0, 0x0000FF));
  }
};
struct DotOverlay : IEffect {
  std::string name = "dot";
  const std::string& id() const override { return name; }
  void render(Canvas& c, int64_t) override { c.setPixel(0, 0, 0xABCDEF); }
};
FillEffect fillEffect;
DotOverlay dotOverlay;
Resources effectResources() {
  static EffectRegistry effects, overlays;
  effects.add(&fillEffect);
  overlays.add(&dotOverlay);
  Resources r = resources();
  r.effects = &effects;
  r.overlays = &overlays;
  return r;
}
bool rejects(const char* json, const char* field) {
  LayoutSpec spec;
  DispatchDetail error;
  if (layout::parse(api::JsonReader(json), spec, error))
    if (PreparedLayout::prepare(spec, {52, 16, false}, effectResources(), std::make_shared<Budget>(), error))
      return false;
  return error.field == field;
}
std::unique_ptr<PreparedLayout> makeWith(const char* json) {
  DispatchDetail error;
  auto p = PreparedLayout::prepare(read(json), {52, 16, false}, effectResources(),
                                   std::make_shared<Budget>(), error);
  TEST_ASSERT_NOT_NULL_MESSAGE(p.get(), error.message.c_str());
  return p;
}
int distinctLit(const Canvas& c) {
  std::vector<uint32_t> seen;
  for (int y = 0; y < c.height(); ++y) for (int x = 0; x < c.width(); ++x) {
    const uint32_t p = c.getPixel(x, y);
    if (p && std::find(seen.begin(), seen.end(), p) == seen.end()) seen.push_back(p);
  }
  return static_cast<int>(seen.size());
}
}

static void test_text_palette_colors_with_region_or_layout_palette() {
  // The pushed-app field names work inside a region.
  auto page = makeWith(R"({"version":1,"regions":[{"id":"v","box":[0,0,52,16],"text":"AAAA","textColor":"palette","palette":"Rainbow"}]})");
  Canvas c(52, 16);
  page->draw(c, {});
  TEST_ASSERT_GREATER_THAN_INT(1, distinctLit(c));
  auto shared = makeWith(R"({"version":1,"palette":["#FF0000","#0000FF"],"regions":[{"id":"v","box":[0,0,52,16],"text":"AAAA","color":"palette"}]})");
  Canvas d(52, 16);
  shared->draw(d, {});
  TEST_ASSERT_GREATER_THAN_INT(1, distinctLit(d));
  TEST_ASSERT_TRUE(rejects(R"({"version":1,"regions":[{"id":"v","box":[0,0,8,8],"text":"A","color":"palette"}]})", "layout.regions.v.color"));
  TEST_ASSERT_TRUE(rejects(R"({"version":1,"regions":[{"id":"v","box":[0,0,8,8],"text":"A","color":"#FFF","textColor":"#FFF"}]})", "layout.regions[0].textColor"));
  TEST_ASSERT_TRUE(rejects(R"({"version":1,"regions":[{"id":"v","box":[0,0,8,8],"text":"A","palette":"Nope"}]})", "layout.regions.v.palette"));
  TEST_ASSERT_TRUE(rejects(R"({"version":1,"regions":[{"id":"v","box":[0,0,8,8],"text":"A","paletteSpeed":2}]})", "layout.regions[0]"));
  TEST_ASSERT_TRUE(rejects(R"({"version":1,"regions":[{"id":"v","box":[0,0,8,8],"icon":"a","palette":"Rainbow"}]})", "layout.regions[0]"));
}

static void test_chart_and_progress_take_palette_colors() {
  auto page = makeWith(R"({"version":1,"palette":["#FF0000","#0000FF"],"regions":[{"id":"p","box":[0,0,20,2],"progress":100,"color":"palette"},{"id":"c","box":[0,4,20,8],"chart":{"values":[1,5,9],"type":"bar"},"color":"palette"}]})");
  Canvas c(52, 16);
  page->draw(c, {});
  TEST_ASSERT_NOT_EQUAL(c.getPixel(0, 0), c.getPixel(19, 0));
  TEST_ASSERT_NOT_EQUAL(c.getPixel(0, 11), c.getPixel(18, 11));
}

static void test_fragments_case_and_pulse_follow_pushed_apps() {
  auto page = makeWith(R"({"version":1,"regions":[{"id":"v","box":[0,0,52,16],"align":"start","text":[{"text":"A","color":"#FF0000"},{"text":"A"}],"color":"#00FF00"}]})");
  Canvas c(52, 16);
  page->draw(c, {});
  bool red = false, green = false;
  for (int y = 0; y < 16; ++y) for (int x = 0; x < 52; ++x) {
    red |= c.getPixel(x, y) == 0xFF0000;
    green |= c.getPixel(x, y) == 0x00FF00;
  }
  TEST_ASSERT_TRUE(red && green);

  auto lower = makeWith(R"({"version":1,"regions":[{"id":"v","box":[0,0,52,16],"text":"a"}]})");
  auto typed = makeWith(R"({"version":1,"regions":[{"id":"v","box":[0,0,52,16],"text":"a","textCase":"asTyped"}]})");
  FrameContext upper;
  upper.uppercase = true;
  Canvas shown(52, 16), kept(52, 16), plain(52, 16);
  lower->draw(shown, upper);
  typed->draw(kept, upper);
  lower->draw(plain, {});
  TEST_ASSERT_EQUAL_INT(1, distinctLit(shown));
  TEST_ASSERT_EQUAL_INT(0, distinctLit(kept));
  TEST_ASSERT_EQUAL_INT(0, distinctLit(plain));

  auto blink = makeWith(R"({"version":1,"regions":[{"id":"v","box":[0,0,52,16],"text":"A","textBlinkMs":1000}]})");
  FrameContext dark, lit;
  dark.nowMs = 100;
  lit.nowMs = 900;
  Canvas off(52, 16), on(52, 16);
  blink->draw(off, dark);
  blink->draw(on, lit);
  TEST_ASSERT_EQUAL_INT(0, distinctLit(off));
  TEST_ASSERT_EQUAL_INT(1, distinctLit(on));
  TEST_ASSERT_TRUE(rejects(R"({"version":1,"regions":[{"id":"v","box":[0,0,8,8],"progress":5,"textBlinkMs":100}]})", "layout.regions[0]"));
}

static void test_draw_regions_are_box_relative_and_clipped() {
  auto page = makeWith(R"({"version":1,"regions":[{"id":"d","box":[5,3,4,4],"color":"#00FF00","draw":[["pixel",0,0,"#FF0000"],["pixel",1,1],["pixel",10,0,"#FF0000"]]}]})");
  Canvas c(52, 16);
  page->draw(c, {});
  TEST_ASSERT_EQUAL_HEX32(0xFF0000, c.getPixel(5, 3));
  TEST_ASSERT_EQUAL_HEX32(0x00FF00, c.getPixel(6, 4));
  TEST_ASSERT_EQUAL_HEX32(0, c.getPixel(15, 3));
  TEST_ASSERT_TRUE(rejects(R"({"version":1,"regions":[{"id":"d","box":[0,0,4,4],"draw":[["pixel",0,0],["blob"]]}]})", "layout.regions[0].draw[1]"));
}

static void test_effect_and_overlay_frame_the_regions() {
  fillEffect.renders = 0;
  auto page = makeWith(R"({"version":1,"effect":"fill","palette":["#FF0000"],"overlay":"dot","regions":[{"id":"v","box":[20,4,10,8],"text":"A","color":"#00FF00"}]})");
  Canvas c(52, 16);
  page->draw(c, {});
  TEST_ASSERT_EQUAL_INT(1, fillEffect.renders);
  TEST_ASSERT_TRUE(fillEffect.sawPalette);
  TEST_ASSERT_FALSE(fillEffect.settings().ramp.valid());
  TEST_ASSERT_EQUAL_HEX32(0xFF0000, c.getPixel(51, 15));
  TEST_ASSERT_EQUAL_HEX32(0xABCDEF, c.getPixel(0, 0));
  TEST_ASSERT_TRUE(rejects(R"({"version":1,"effect":"nope","regions":[{"id":"v","box":[0,0,8,8],"text":"A"}]})", "layout.effect"));
  TEST_ASSERT_TRUE(rejects(R"({"version":1,"overlay":"nope","regions":[{"id":"v","box":[0,0,8,8],"text":"A"}]})", "layout.overlay"));
  TEST_ASSERT_TRUE(rejects(R"({"version":1,"effect":"fill","backgroundColor":"#111","regions":[{"id":"v","box":[0,0,8,8],"text":"A"}]})", "layout.backgroundColor"));
}

int main(int,char**) {
  UNITY_BEGIN();
  RUN_TEST(test_text_palette_colors_with_region_or_layout_palette);
  RUN_TEST(test_chart_and_progress_take_palette_colors);
  RUN_TEST(test_fragments_case_and_pulse_follow_pushed_apps);
  RUN_TEST(test_draw_regions_are_box_relative_and_clipped);
  RUN_TEST(test_effect_and_overlay_frame_the_regions);
  RUN_TEST(test_image_regions_refuse_icon_text_they_cannot_show);
  RUN_TEST(test_url_pictures_are_fitted_to_their_region_other_images_to_the_display);
  RUN_TEST(test_parser_is_strict_atomic_and_does_not_change_legacy_payloads);
  RUN_TEST(test_layout_payload_requires_a_taller_registered_display);
  RUN_TEST(test_free_regions_clip_original_fonts_and_restore_parent_clip);
  RUN_TEST(test_duplicate_ids_unknown_fonts_and_out_of_bounds_boxes_reject);
  RUN_TEST(test_native_motion_is_independent_of_frame_count_and_aggregates_repeat);
  RUN_TEST(test_reorder_keeps_animation_by_id_and_failed_updates_keep_frame);
  RUN_TEST(test_untrusted_global_scroll_limits_are_bounded_without_changing_settings);
  RUN_TEST(test_shared_budget_rejects_peak_and_returns_charge_after_release);
  RUN_TEST(test_chart_points_and_progress_use_region_geometry);
  RUN_TEST(test_images_fully_decode_before_admission_and_failed_reload_keeps_old_pixels);
  RUN_TEST(test_image_updates_reuse_decode_and_keep_old_page_on_other_region_failure);
  RUN_TEST(test_pipeline_notifications_and_admission_are_native_and_atomic);
  RUN_TEST(test_enlarged_pages_leave_a_layout_at_panel_size);
  RUN_TEST(test_asset_dimension_change_reprepares_atomically);
  RUN_TEST(test_stale_scroll_completion_cannot_end_updated_layout);
  RUN_TEST(test_settings_and_assets_clear_completion_before_engine_tick);
  RUN_TEST(test_asset_invalidation_reaches_hidden_and_queued_layouts);
  RUN_TEST(test_storage_failure_rejects_parse_and_update_without_partial_replacement);
  RUN_TEST(test_payload_admission_failure_keeps_the_published_content);
  RUN_TEST(test_checked_string_decode_keeps_unicode_and_rejects_unbounded_options);
  RUN_TEST(test_every_string_allocation_failure_keeps_the_short_oom_diagnostic);
  return UNITY_END();
}
