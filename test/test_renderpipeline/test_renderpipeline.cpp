#include "../EngineFakes.h"
#include "core/sound/RoutedPcmSink.h"
#include <unity.h>

#include <algorithm>
#include <string>
#include <vector>

#include "core/CoreEngine.h"
#include "core/Transitions.h"
#include "core/apps/builtin/TimeApp.h"
#include "core/render/RenderPipeline.h"

using namespace awtrix;

namespace {

#define G {0, 3, 3, 4, 0, 0}
const FontGlyph kG[] = {G, G, G, G, G, G, G, G, G, G,
                        G, G, G, G, G, G, G, G, G, G};
#undef G
const uint8_t kB[] = {0xFF, 0x80};
const GfxFont kFont = {kB, kG, '.', 'A', 8};

#define W {0, 3, 3, 8, 0, 0}
const FontGlyph kWideG[] = {W, W, W, W, W, W, W, W, W, W,
                            W, W, W, W, W, W, W, W, W, W};
#undef W
const GfxFont kWideFont = {kB, kWideG, '.', 'A', 8};
// small and large as on a device; tall is a font whose line box fills all eight rows.
const FontEntry kFontEntries[] = {
    {"small", &kFont, 6, 1, 8}, {"large", &kWideFont, 6, 2, 8}, {"tall", &kFont, 8, 0, 8}};
const FontCatalog kFonts{kFontEntries, 3};

using FDisplay = awtrix::test::NullDisplay;
using FSystem = awtrix::test::NullSystem;

struct FakeIcon : IPageIcon {
  int begins = 0, clears = 0, advances = 0, blits = 0;
  bool beginOk = true;
  IconLoad failAs = IconLoad::kOom;
  int w = 8;
  int lastBlitX = -1;
  int lastBlitY = -1;
  int maxWidth = 0, maxHeight = 0;
  int64_t lastAdvanceMs = 0;
  int64_t firstAdvanceMs = -1;
  std::string failId;
  bool factoryOk = true;
  int factoryMinClears = 0;
  mutable int creates = 0;
  int destroys = 0;
  mutable std::vector<FakeIcon*> children;
  FakeIcon* factoryOwner = nullptr;
  std::string lastId;
  ~FakeIcon() override {
    if (factoryOwner) ++factoryOwner->destroys;
  }
  IconLoad begin(const std::string& id, int width, int height) override {
    ++begins;
    lastId = id;
    maxWidth = width;
    maxHeight = height;
    const FakeIcon& rules = factoryOwner ? *factoryOwner : *this;
    if (!rules.failId.empty() && id == rules.failId) return rules.failAs;
    return beginOk && rules.beginOk ? IconLoad::kGood : rules.failAs;
  }
  void clear() override { ++clears; }
  void advance(int64_t nowMs) override {
    if (advances++ == 0) firstAdvanceMs = nowMs;
    lastAdvanceMs = nowMs;
  }
  void blit(Canvas& dst, int xOffset, int yOffset = 0) const override {
    auto* self = const_cast<FakeIcon*>(this);
    self->blits++;
    self->lastBlitX = xOffset;
    self->lastBlitY = yOffset;
    const uint32_t color = lastId == "red" ? 0xFF0000u :
                           lastId == "green" ? 0x00FF00u : 0xABCDEFu;
    for (int y = 0; y < 8; ++y)
      for (int x = 0; x < w; ++x) dst.setPixel(x + xOffset, y + yOffset, color);
  }
  int width() const override { return w; }
  std::unique_ptr<IPageIcon> create() const override {
    if (!factoryOk || clears < factoryMinClears) return nullptr;
    auto child = std::make_unique<FakeIcon>();
    child->factoryOwner = const_cast<FakeIcon*>(this);
    children.push_back(child.get());
    ++creates;
    return child;
  }
};

struct CaptureEffect : IEffect {
  std::string id_{"capture"};
  long lastFrame = -1;
  EffectSettings lastSettings;
  const std::string& id() const override { return id_; }
  float rate() const override { return rate::kContinuous; }
  void render(Canvas&, int64_t frame) override { lastFrame = frame; }
  void setSettings(const EffectSettings& s) override { lastSettings = s; settings_ = s; }
};

struct SlowCaptureEffect : CaptureEffect {
  SlowCaptureEffect() { id_ = "slow"; }
  float rate() const override { return rate::kSteady; }
};

// The pipeline no longer owns any sound policy, so the counter sits where the sound really lands.
struct FakeTone : sound::IToneSink {
  int plays = 0;
  int rtttlPlays = 0;
  int melodyPlays = 0;
  int stops = 0;
  bool playing = false;
  std::vector<sound::Group> groups;
  void begin() override {}
  void setVolumes(const sound::Volumes&) override {}
  bool playRtttl(const std::string&, sound::Group group) override {
    ++plays;
    ++rtttlPlays;
    groups.push_back(group);
    return true;
  }
  bool playMelodyFile(const std::string&, sound::Group group) override {
    ++plays;
    ++melodyPlays;
    groups.push_back(group);
    return true;
  }
  void stop() override {
    ++stops;
    playing = false;
  }
  void tick() override {}
  bool isPlaying() const override { return playing; }
};

// The speaker's MP3 side and the files it finds, for the MP3 a notification names.
struct FakePcm : sound::RoutedPcmSink {
  // A voice that speaks every text without "bad" in it.
  bool voiceAvailable = false, voicePlaying = false;
  std::vector<std::string> speech;
  bool speaks() const override { return voiceAvailable; }
  bool checkSpeech(const std::string& text, DispatchDetail& detail) override {
    if (text.find("bad") == std::string::npos) return true;
    detail.message = "no words to speak";
    return false;
  }
  DispatchResult playSpeech(const std::string& text, sound::Group,
                            DispatchDetail& detail) override {
    if (!checkSpeech(text, detail)) return DispatchResult::ValidationError;
    speech.push_back(text);
    voicePlaying = true;
    return DispatchResult::Ok;
  }
  std::vector<std::string> mp3s;
  int stops = 0;
  void setVolumes(const sound::Volumes&) override {}
  bool playMp3(const std::string& path, sound::Group) override {
    mp3s.push_back(path);
    return true;
  }
  void stopOneShot() override {
    ++stops;
    voicePlaying = false;
  }
  bool oneShotPlaying() const override { return voicePlaying; }
  DispatchResult playStream(const std::string&, const std::string&, DispatchDetail&) override {
    return DispatchResult::Ok;
  }
  void stopStream() override {}
  void tick(int64_t) override {}
};

struct FakeFiles : sound::IAssetProbe {
  std::vector<std::string> paths;
  bool hasFile(const std::string& path) const override {
    return std::find(paths.begin(), paths.end(), path) != paths.end();
  }
};

struct FakeClock : IPageClock {
  void fill(RenderCtx& ctx, int64_t nowMs) override {
    ctx.nowMs = nowMs;
    ctx.hour = 12;
    ctx.minute = 0;
    ctx.second = 0;
    ctx.weekday = 1;
    ctx.mday = 1;
    ctx.month = 1;
    ctx.year = 2026;
  }
};

struct FakeZoom : IPageZoom {
  bool on;
  Canvas page;
  mutable int presents = 0;
  FakeZoom(int stageWidth, int stageHeight, bool enlarge = true)
      : on(enlarge), page(stageWidth, stageHeight) {}
  bool enlarges(const Settings&, const AppSpec&, uint32_t) override { return on; }
  Canvas& stage() override { return page; }
  void present(Canvas&) const override { ++presents; }
};

struct SizeEffect : CaptureEffect {
  int width = 0;
  void render(Canvas& c, int64_t frame) override {
    width = c.width();
    CaptureEffect::render(c, frame);
  }
};

struct Rig {
  FDisplay di; FSystem sy;
  FakeTone tone;
  sound::AudioRouter audio;
  CoreEngine engine{audio, di, sy};
  AppRegistry apps;
  EffectRegistry effects, overlays;
  FakeIcon icons;
  FakeClock clock;
  Canvas canvas{32, 8};
  FakeIcon iconsB;
  RenderPipeline* pipe = nullptr;

  Rig(int width = 32, int height = 8, IExternalPage* external = nullptr, IPageZoom* zoom = nullptr)
      : canvas(width, height) {
    audio.setTone(&tone);
    RenderPipelineDeps d;
    d.external = external;
    d.zoom = zoom;
    d.engine = &engine;
    d.apps = &apps;
    d.effects = &effects;
    d.overlays = &overlays;
    d.fonts = &kFonts;
    d.icons = &icons;
    d.iconsB = &iconsB;
    d.audio = &audio;
    d.clock = &clock;
    pipe = new RenderPipeline(width, height, d);
  }
  ~Rig() { delete pipe; }
};

int firstLitColumn(const Canvas& c) {
  for (int x = 0; x < c.width(); ++x)
    for (int y = 0; y < c.height(); ++y) {
      const uint32_t p = c.getPixel(x, y);
      if (((p >> 16) & 0xFF) > 100 || ((p >> 8) & 0xFF) > 100 || (p & 0xFF) > 100) return x;
    }
  return -1;
}

int litColumns(const Canvas& c) {
  int n = 0;
  for (int x = 0; x < c.width(); ++x)
    for (int y = 0; y < c.height(); ++y) {
      const uint32_t p = c.getPixel(x, y);
      if (((p >> 16) & 0xFF) > 100 || ((p >> 8) & 0xFF) > 100 || (p & 0xFF) > 100) { ++n; break; }
    }
  return n;
}

void runUntil(Rig& r, int64_t fromMs, int64_t toMs) {
  for (int64_t t = fromMs; t <= toMs; t += 50) {
    r.engine.tick(t);
    r.pipe->renderFrame(r.canvas, t);
  }
}

Command cmd(CommandType t, const std::string& name = "", const std::string& payload = "") {
  Command c(t);
  c.name = name;
  c.payload = payload;
  return c;
}

Command switchFast(const std::string& name) {
  return cmd(CommandType::SwitchApp, "", "{\"name\":\"" + name + "\",\"fast\":true}");
}

}

void setUp() {}
void tearDown() {}

static void test_the_selected_font_decides_the_scroll_width() {
  Rig r;
  r.engine.execute(cmd(CommandType::SetPushedApp, "narrow", "{\"text\":\"AAAAA\"}"));
  r.engine.tick(0);
  r.engine.execute(switchFast("narrow"));
  r.engine.tick(10);
  r.pipe->renderFrame(r.canvas, 10);
  r.pipe->renderFrame(r.canvas, 4000);
  TEST_ASSERT_EQUAL_FLOAT(0.0f, r.pipe->textX());

  Rig w;
  w.engine.execute(cmd(CommandType::SetPushedApp, "wide",
                       "{\"text\":\"AAAAA\",\"font\":\"large\"}"));
  w.engine.tick(0);
  w.engine.execute(switchFast("wide"));
  w.engine.tick(10);
  w.pipe->renderFrame(w.canvas, 10);
  w.pipe->renderFrame(w.canvas, 4000);
  TEST_ASSERT_TRUE_MESSAGE(w.pipe->textX() < 0.0f, "wide text should have scrolled");
}

static int topLitRow(const Canvas& c) {
  for (int y = 0; y < c.height(); ++y)
    for (int x = 0; x < c.width(); ++x)
      if (c.getPixel(x, y)) return y;
  return -1;
}

static int pageTopRow(const std::string& payload, int rows = 16) {
  Rig r(32, rows);
  r.engine.execute(cmd(CommandType::SetPushedApp, "p", payload));
  r.engine.tick(0);
  r.engine.execute(switchFast("p"));
  r.engine.tick(10);
  r.pipe->renderFrame(r.canvas, 10);
  return topLitRow(r.canvas);
}

static void test_a_page_font_sits_by_its_own_metrics() {
  for (const auto& font : kFontEntries) {
    const std::string payload = std::string("{\"text\":\"A\",\"font\":\"") + font.name + "\"}";
    for (int rows : {16, 32})
      TEST_ASSERT_EQUAL_INT(pageBaseline(font, rows) + kG[0].yOffset, pageTopRow(payload, rows));
  }
}

static void test_draw_text_keeps_its_absolute_row() {
  const auto small = R"({"draw":[["text",0,1,"A"]]})";
  const auto tall = R"({"font":"tall","draw":[["text",0,1,"A"]]})";
  TEST_ASSERT_EQUAL_INT(pageTopRow(small, 8), pageTopRow(small, 16));
  TEST_ASSERT_EQUAL_INT(pageTopRow(tall, 16), pageTopRow(tall, 32));
  TEST_ASSERT_EQUAL_INT(kFonts.find("tall")->ascent - kFonts.small().ascent,
                        pageTopRow(tall) - pageTopRow(small));
}

static void test_an_unknown_page_font_is_refused() {
  Rig r;
  DispatchDetail detail;
  TEST_ASSERT_EQUAL_INT(static_cast<int>(DispatchResult::ValidationError),
                        static_cast<int>(r.engine.setPushedApp("p", R"({"text":"A","font":"nope"})", detail)));
  TEST_ASSERT_EQUAL_STRING("font", detail.field.c_str());
  TEST_ASSERT_NULL(r.engine.pushedApp("p"));
}

static void test_scroll_holds_then_advances_and_resets_on_page_change() {
  Rig r;
  r.engine.execute(cmd(CommandType::SetPushedApp, "a", "{\"text\":\"AAAAAAAAAAAA\"}"));
  r.engine.tick(0);
  r.engine.execute(switchFast("a"));
  r.engine.tick(10);

  r.pipe->renderFrame(r.canvas, 10);
  TEST_ASSERT_EQUAL_FLOAT(0.0f, r.pipe->textX());
  r.pipe->renderFrame(r.canvas, 500);
  TEST_ASSERT_EQUAL_FLOAT(0.0f, r.pipe->textX());
  r.pipe->renderFrame(r.canvas, 2000);
  TEST_ASSERT_TRUE(r.pipe->textX() < 0.0f);

  r.engine.execute(cmd(CommandType::SetPushedApp, "b", "{\"text\":\"AAAAAAAAAAAA\"}"));
  r.engine.execute(switchFast("b"));
  r.engine.tick(2100);
  r.pipe->renderFrame(r.canvas, 2100);
  TEST_ASSERT_EQUAL_FLOAT(0.0f, r.pipe->textX());
}

static void test_effect_frame_derived_from_wall_clock() {
  Rig r;
  CaptureEffect fx;
  r.overlays.add(&fx);
  r.engine.execute(cmd(CommandType::SetPushedApp, "e", "{\"text\":\"A\",\"overlay\":\"capture\"}"));
  r.engine.execute(switchFast("e"));
  r.engine.tick(0);
  r.pipe->renderFrame(r.canvas, 2400);
  TEST_ASSERT_EQUAL_INT(100, (int)fx.lastFrame);
  r.pipe->renderFrame(r.canvas, 2400);
  TEST_ASSERT_EQUAL_INT(100, (int)fx.lastFrame);
  r.pipe->renderFrame(r.canvas, 4800);
  TEST_ASSERT_EQUAL_INT(200, (int)fx.lastFrame);
}

static void test_declared_rate_scales_the_step_count() {
  Rig r;
  SlowCaptureEffect fx;
  r.overlays.add(&fx);
  r.engine.execute(cmd(CommandType::SetPushedApp, "e", "{\"text\":\"A\",\"overlay\":\"slow\"}"));
  r.engine.execute(switchFast("e"));
  r.engine.tick(0);
  r.pipe->renderFrame(r.canvas, 2400);
  TEST_ASSERT_EQUAL_INT(36, (int)fx.lastFrame);
}

static void test_per_app_overlay_uses_the_apps_effect_settings() {
  Rig r;
  CaptureEffect fx;
  r.overlays.add(&fx);
  r.engine.execute(cmd(CommandType::SetPushedApp, "e",
                       "{\"text\":\"A\",\"overlay\":\"capture\",\"effectSpeed\":3}"));
  r.engine.execute(switchFast("e"));
  r.engine.tick(0);
  r.pipe->renderFrame(r.canvas, 2400);
  TEST_ASSERT_TRUE(fx.lastSettings.hasSpeed);
  TEST_ASSERT_EQUAL_FLOAT(3.0f, fx.lastSettings.speed);
}

static void test_global_overlay_uses_the_global_settings() {
  Rig r;
  CaptureEffect fx;
  r.overlays.add(&fx);
  r.engine.execute(cmd(CommandType::SetDisplay, "",
                       "{\"overlay\":\"capture\",\"overlaySettings\":{\"speed\":0.5}}"));
  r.engine.execute(cmd(CommandType::SetPushedApp, "e", "{\"text\":\"A\"}"));
  r.engine.execute(switchFast("e"));
  r.engine.tick(0);
  r.pipe->renderFrame(r.canvas, 2400);
  TEST_ASSERT_TRUE(fx.lastSettings.hasSpeed);
  TEST_ASSERT_EQUAL_FLOAT(0.5f, fx.lastSettings.speed);
}

static void test_app_effect_settings_do_not_reach_the_global_overlay() {
  Rig r;
  CaptureEffect fx;
  r.overlays.add(&fx);
  r.engine.execute(cmd(CommandType::SetDisplay, "", "{\"overlay\":\"capture\"}"));
  r.engine.execute(cmd(CommandType::SetPushedApp, "e",
                       "{\"text\":\"A\",\"effectSpeed\":9}"));
  r.engine.execute(switchFast("e"));
  r.engine.tick(0);
  r.pipe->renderFrame(r.canvas, 2400);
  TEST_ASSERT_FALSE(fx.lastSettings.hasSpeed);
}

static void test_missing_icon_uses_iconless_layout() {
  Rig r;
  r.icons.beginOk = false;
  r.engine.execute(cmd(CommandType::SetPushedApp, "ic", "{\"text\":\"A\",\"icon\":\"nope\"}"));
  r.engine.execute(switchFast("ic"));
  r.engine.tick(0);
  r.pipe->renderFrame(r.canvas, 0);
  TEST_ASSERT_EQUAL_INT(1, r.icons.begins);
  TEST_ASSERT_EQUAL_INT(0, r.icons.blits);
  bool litLeftHalf = false;
  for (int y = 0; y < 8; ++y)
    for (int x = 0; x < 16; ++x)
      if (r.canvas.getPixel(x, y) != 0) litLeftHalf = true;
  TEST_ASSERT_TRUE(litLeftHalf);
}

static void test_missing_icon_frees_the_scroll_column_too() {
  auto firstLitColumn = [](Rig& rig) {
    for (int x = 0; x < rig.canvas.width(); ++x)
      for (int y = 0; y < 8; ++y)
        if (rig.canvas.getPixel(x, y) != 0) return x;
    return -1;
  };
  Rig broken;
  broken.icons.beginOk = false;
  broken.engine.execute(cmd(CommandType::SetPushedApp, "ic",
                            "{\"text\":\"AAAAAAAAAAAA\",\"icon\":\"nope\"}"));
  broken.engine.execute(switchFast("ic"));
  broken.engine.tick(0);
  broken.pipe->renderFrame(broken.canvas, 0);

  Rig none;
  none.engine.execute(cmd(CommandType::SetPushedApp, "ic",
                          "{\"text\":\"AAAAAAAAAAAA\"}"));
  none.engine.execute(switchFast("ic"));
  none.engine.tick(0);
  none.pipe->renderFrame(none.canvas, 0);

  TEST_ASSERT_EQUAL_INT(0, broken.icons.blits);
  TEST_ASSERT_EQUAL_INT(firstLitColumn(none), firstLitColumn(broken));
}

static void test_icon_is_centered_vertically() {
  for (int rows : {8, 16}) {
    Rig r(32, rows);
    r.engine.execute(cmd(CommandType::SetPushedApp, "ic", "{\"text\":\"A\",\"icon\":\"red\"}"));
    r.engine.execute(switchFast("ic"));
    r.engine.tick(0);
    r.pipe->renderFrame(r.canvas, 0);
    TEST_ASSERT_EQUAL_INT(1, r.icons.blits);
    TEST_ASSERT_EQUAL_INT((rows - 8) / 2, r.icons.lastBlitY);
  }
}

static void test_fullscreen_icon_is_background_not_column() {
  Rig full;
  full.icons.w = 32;
  full.engine.execute(cmd(CommandType::SetPushedApp, "ic",
                          "{\"text\":\"AAAAAAAAAAAA\",\"icon\":\"wide\"}"));
  full.engine.execute(switchFast("ic"));
  full.engine.tick(0);
  full.pipe->renderFrame(full.canvas, 0);

  Rig none;
  none.engine.execute(cmd(CommandType::SetPushedApp, "ic",
                          "{\"text\":\"AAAAAAAAAAAA\"}"));
  none.engine.execute(switchFast("ic"));
  none.engine.tick(0);
  none.pipe->renderFrame(none.canvas, 0);

  TEST_ASSERT_EQUAL_INT(1, full.icons.blits);
  TEST_ASSERT_EQUAL_INT(0, full.icons.lastBlitX);

  auto firstTextColumn = [](Rig& rig) {
    for (int x = 0; x < rig.canvas.width(); ++x)
      for (int y = 0; y < 8; ++y)
        if (rig.canvas.getPixel(x, y) != 0 && rig.canvas.getPixel(x, y) != 0xABCDEFu) return x;
    return -1;
  };
  TEST_ASSERT_EQUAL_INT(firstTextColumn(none), firstTextColumn(full));
}

static void test_fullscreen_icon_survives_and_text_draws_over_it() {
  Rig r;
  r.icons.w = 32;
  r.engine.execute(cmd(CommandType::SetPushedApp, "ic", "{\"text\":\"A\",\"icon\":\"wide\"}"));
  r.engine.execute(switchFast("ic"));
  r.engine.tick(0);
  r.pipe->renderFrame(r.canvas, 0);

  int iconPixels = 0, textPixels = 0;
  for (int y = 0; y < 8; ++y)
    for (int x = 0; x < 32; ++x) {
      const uint32_t p = r.canvas.getPixel(x, y);
      if (p == 0xABCDEFu) ++iconPixels;
      else if (p != 0) ++textPixels;
    }
  TEST_ASSERT_TRUE(iconPixels > 0);
  TEST_ASSERT_TRUE(textPixels > 0);
}

static void test_icon_decoded_once_per_page_but_advanced_every_frame() {
  Rig r;
  r.engine.execute(cmd(CommandType::SetPushedApp, "ic", "{\"text\":\"A\",\"icon\":\"42\"}"));
  r.engine.execute(switchFast("ic"));
  r.engine.tick(0);

  r.pipe->renderFrame(r.canvas, 0);
  r.pipe->renderFrame(r.canvas, 20);
  r.pipe->renderFrame(r.canvas, 40);

  TEST_ASSERT_EQUAL_INT(1, r.icons.begins);
  TEST_ASSERT_EQUAL_STRING("42", r.icons.lastId.c_str());
  TEST_ASSERT_EQUAL_INT(3, r.icons.advances);
  TEST_ASSERT_EQUAL_INT(3, r.icons.blits);
}

static void test_failed_icon_retries_on_timer_and_heals() {
  Rig r;
  r.icons.beginOk = false;
  r.engine.execute(cmd(CommandType::SetPushedApp, "ic", "{\"text\":\"A\",\"icon\":\"42\"}"));
  r.engine.execute(switchFast("ic"));
  r.engine.tick(0);

  r.pipe->renderFrame(r.canvas, 0);
  r.pipe->renderFrame(r.canvas, 20);
  r.pipe->renderFrame(r.canvas, 4999);
  TEST_ASSERT_EQUAL_INT(1, r.icons.begins);

  r.pipe->renderFrame(r.canvas, 5000);
  TEST_ASSERT_EQUAL_INT(2, r.icons.begins);

  r.icons.beginOk = true;
  r.pipe->renderFrame(r.canvas, 10000);
  TEST_ASSERT_EQUAL_INT(3, r.icons.begins);
  r.pipe->renderFrame(r.canvas, 10020);
  r.pipe->renderFrame(r.canvas, 60000);
  TEST_ASSERT_EQUAL_INT(3, r.icons.begins);
  TEST_ASSERT_TRUE(r.icons.blits > 0);
}

static void test_panel_bounds_reach_pushed_app_and_notification_icons() {
  Rig r(51, 16);
  r.engine.execute(cmd(CommandType::SetPushedApp, "ic", "{\"icon\":\"pushed\"}"));
  r.engine.execute(switchFast("ic"));
  r.engine.tick(0);
  r.pipe->renderFrame(r.canvas, 0);
  TEST_ASSERT_EQUAL_STRING("pushed", r.icons.lastId.c_str());
  TEST_ASSERT_EQUAL_INT(51, r.icons.maxWidth);
  TEST_ASSERT_EQUAL_INT(16, r.icons.maxHeight);

  r.engine.execute(cmd(CommandType::Notify, "", "{\"icon\":\"notification\"}"));
  r.engine.tick(10);
  r.pipe->renderFrame(r.canvas, 10);
  const auto& icon = r.icons.lastId == "notification" ? r.icons : r.iconsB;
  TEST_ASSERT_EQUAL_STRING("notification", icon.lastId.c_str());
  TEST_ASSERT_EQUAL_INT(51, icon.maxWidth);
  TEST_ASSERT_EQUAL_INT(16, icon.maxHeight);
}

static void test_an_enlarged_page_is_drawn_on_the_stage_with_its_bounds() {
  FakeZoom zoom(26, 8);
  Rig r(52, 16, nullptr, &zoom);
  r.engine.execute(cmd(CommandType::SetPushedApp, "ic",
                       R"({"text":"A","icon":"pushed","icons":[{"icon":"red","x":1,"y":1}]})"));
  r.engine.execute(switchFast("ic"));
  r.engine.tick(0);
  r.pipe->renderFrame(r.canvas, 0);
  r.pipe->renderFrame(r.canvas, 50);
  TEST_ASSERT_EQUAL_INT(26, r.icons.maxWidth);
  TEST_ASSERT_EQUAL_INT(8, r.icons.maxHeight);
  TEST_ASSERT_EQUAL_INT(1, static_cast<int>(r.icons.children.size()));
  TEST_ASSERT_EQUAL_INT(26, r.icons.children[0]->maxWidth);
  TEST_ASSERT_EQUAL_INT(8, r.icons.children[0]->maxHeight);
  TEST_ASSERT_EQUAL_INT(0, r.icons.lastBlitY);
  TEST_ASSERT_EQUAL_INT(2, zoom.presents);
  TEST_ASSERT_TRUE(litColumns(zoom.page) > 8);

  r.engine.execute(cmd(CommandType::Notify, "", R"({"text":"A","hold":true})"));
  r.engine.tick(100);
  r.pipe->renderFrame(r.canvas, 100);
  TEST_ASSERT_EQUAL_INT(3, zoom.presents);
}

static void test_an_enlarged_page_scrolls_by_the_stage_width() {
  auto scrolls = [](IPageZoom* zoom) {
    Rig r(52, 16, nullptr, zoom);
    r.engine.execute(cmd(CommandType::SetPushedApp, "p", R"({"text":"AAAAAAAAAA"})"));
    r.engine.tick(0);
    r.engine.execute(switchFast("p"));
    r.engine.tick(10);
    r.pipe->renderFrame(r.canvas, 10);
    r.pipe->renderFrame(r.canvas, 4000);
    return r.pipe->textX() < 0.0f;
  };
  FakeZoom zoom(26, 8);
  TEST_ASSERT_FALSE(scrolls(nullptr));
  TEST_ASSERT_TRUE(scrolls(&zoom));
}

static void test_switching_the_zoom_reopens_icons_for_the_new_bounds() {
  FakeZoom zoom(26, 8, false);
  Rig r(52, 16, nullptr, &zoom);
  r.engine.execute(cmd(CommandType::SetPushedApp, "ic", R"({"icon":"pushed"})"));
  r.engine.execute(switchFast("ic"));
  r.engine.tick(0);
  r.pipe->renderFrame(r.canvas, 0);
  TEST_ASSERT_EQUAL_INT(52, r.icons.maxWidth);
  TEST_ASSERT_EQUAL_INT(0, zoom.presents);

  zoom.on = true;
  const int begins = r.icons.begins;
  r.pipe->renderFrame(r.canvas, 50);
  TEST_ASSERT_EQUAL_INT(begins + 1, r.icons.begins);
  TEST_ASSERT_EQUAL_INT(26, r.icons.maxWidth);
  TEST_ASSERT_EQUAL_INT(1, zoom.presents);
}

static void test_the_overlay_of_an_enlarged_page_covers_the_whole_panel() {
  FakeZoom zoom(26, 8);
  Rig r(52, 16, nullptr, &zoom);
  SizeEffect fx;
  r.overlays.add(&fx);
  r.engine.execute(cmd(CommandType::SetDisplay, "", R"({"overlay":"capture"})"));
  r.engine.execute(cmd(CommandType::SetPushedApp, "p", R"({"text":"A"})"));
  r.engine.execute(switchFast("p"));
  r.engine.tick(0);
  r.pipe->renderFrame(r.canvas, 0);
  TEST_ASSERT_EQUAL_INT(1, zoom.presents);
  TEST_ASSERT_EQUAL_INT(52, fx.width);
}

static void test_builtin_apps_are_not_enlarged() {
  FakeZoom zoom(26, 8);
  Rig r(52, 16, nullptr, &zoom);
  TimeApp timeApp;
  r.apps.add(&timeApp);
  r.engine.execute(switchFast("Time"));
  r.engine.tick(0);
  r.pipe->renderFrame(r.canvas, 0);
  TEST_ASSERT_EQUAL_INT(0, zoom.presents);
  TEST_ASSERT_EQUAL_INT(0, litColumns(zoom.page));
  TEST_ASSERT_TRUE(litColumns(r.canvas) > 0);
}

static void test_multiple_icons_render_in_payload_order_at_absolute_coordinates() {
  Rig r(51, 16);
  r.engine.execute(cmd(CommandType::SetPushedApp, "icons",
      "{\"icons\":[{\"icon\":\"red\",\"x\":1,\"y\":1},"
      "{\"icon\":\"green\",\"x\":5,\"y\":5}]}"));
  r.engine.execute(switchFast("icons"));
  r.engine.tick(0);
  r.pipe->renderFrame(r.canvas, 0);
  r.pipe->renderFrame(r.canvas, 25);
  TEST_ASSERT_EQUAL_HEX32(0xFF0000u, r.canvas.getPixel(1, 1));
  TEST_ASSERT_EQUAL_HEX32(0x00FF00u, r.canvas.getPixel(5, 5));
  TEST_ASSERT_EQUAL_HEX32(0x00FF00u, r.canvas.getPixel(12, 12));
  TEST_ASSERT_EQUAL_HEX32(0u, r.canvas.getPixel(0, 0));
  TEST_ASSERT_EQUAL_INT(2, r.icons.creates);
  TEST_ASSERT_EQUAL_INT(51, r.icons.children[0]->maxWidth);
  TEST_ASSERT_EQUAL_INT(16, r.icons.children[1]->maxHeight);
  TEST_ASSERT_EQUAL_INT(0, r.icons.begins);
  r.pipe->renderFrame(r.canvas, 100);
  TEST_ASSERT_EQUAL_INT(1, r.icons.children[0]->begins);
  TEST_ASSERT_EQUAL_INT(1, r.icons.children[1]->begins);
  TEST_ASSERT_EQUAL_INT(2, r.icons.children[0]->advances);
  TEST_ASSERT_EQUAL_INT(2, r.icons.children[1]->advances);
}

static void test_icons_of_a_page_start_animating_on_the_same_frame() {
  Rig r;
  r.engine.execute(cmd(CommandType::SetPushedApp, "icons",
      "{\"icon\":\"red\",\"icons\":[{\"icon\":\"red\",\"x\":12},"
      "{\"icon\":\"red\",\"x\":22}]}"));
  r.engine.execute(switchFast("icons"));
  r.engine.tick(0);
  for (int now = 0; now <= 100; now += 25) r.pipe->renderFrame(r.canvas, now);
  TEST_ASSERT_EQUAL_INT(2, r.icons.creates);
  TEST_ASSERT_EQUAL_INT64(50, r.icons.firstAdvanceMs);
  TEST_ASSERT_EQUAL_INT64(50, r.icons.children[0]->firstAdvanceMs);
  TEST_ASSERT_EQUAL_INT64(50, r.icons.children[1]->firstAdvanceMs);
  TEST_ASSERT_EQUAL_INT(r.icons.advances, r.icons.children[0]->advances);
  TEST_ASSERT_EQUAL_INT(r.icons.advances, r.icons.children[1]->advances);
}

static void test_an_icon_waiting_for_memory_does_not_hold_back_the_others() {
  Rig r;
  r.icons.failId = "big";
  r.icons.failAs = IconLoad::kOom;
  r.engine.execute(cmd(CommandType::SetPushedApp, "icons",
      "{\"icons\":[{\"icon\":\"big\",\"x\":0},{\"icon\":\"red\",\"x\":12}]}"));
  r.engine.execute(switchFast("icons"));
  r.engine.tick(0);
  for (int now = 0; now <= 100; now += 25) r.pipe->renderFrame(r.canvas, now);
  TEST_ASSERT_EQUAL_INT(2, r.icons.creates);
  TEST_ASSERT_EQUAL_INT(0, r.icons.children[0]->advances);
  TEST_ASSERT_EQUAL_INT64(25, r.icons.children[1]->firstAdvanceMs);
  TEST_ASSERT_EQUAL_HEX32(0xFF0000u, r.canvas.getPixel(12, 0));
}

static void test_additional_icons_load_one_per_frame_after_the_page_icon() {
  Rig r;
  r.engine.execute(cmd(CommandType::SetPushedApp, "icons",
      "{\"icon\":\"main\",\"icons\":[{\"icon\":\"red\",\"x\":12},"
      "{\"icon\":\"green\",\"x\":22}]}"));
  r.engine.execute(switchFast("icons"));
  r.engine.tick(0);
  r.pipe->renderFrame(r.canvas, 0);
  TEST_ASSERT_EQUAL_INT(1, r.icons.begins);
  TEST_ASSERT_EQUAL_INT(0, r.icons.creates);
  r.pipe->renderFrame(r.canvas, 25);
  TEST_ASSERT_EQUAL_INT(1, r.icons.creates);
  r.pipe->renderFrame(r.canvas, 50);
  TEST_ASSERT_EQUAL_INT(2, r.icons.creates);
  TEST_ASSERT_EQUAL_HEX32(0xFF0000u, r.canvas.getPixel(12, 0));
  TEST_ASSERT_EQUAL_HEX32(0x00FF00u, r.canvas.getPixel(22, 0));
  r.pipe->renderFrame(r.canvas, 75);
  TEST_ASSERT_EQUAL_INT(1, r.icons.begins);
  TEST_ASSERT_EQUAL_INT(1, r.icons.children[0]->begins);
  TEST_ASSERT_EQUAL_INT(1, r.icons.children[1]->begins);
}

static void test_missing_icons_wait_for_an_asset_change_instead_of_a_timer() {
  Rig r;
  r.icons.beginOk = false;
  r.icons.failAs = IconLoad::kMissing;
  r.engine.execute(cmd(CommandType::SetPushedApp, "ic",
      "{\"text\":\"A\",\"icon\":\"nope\",\"icons\":[{\"icon\":\"red\",\"x\":16}]}"));
  r.engine.execute(switchFast("ic"));
  r.engine.tick(0);
  r.pipe->renderFrame(r.canvas, 0);
  r.pipe->renderFrame(r.canvas, 25);
  TEST_ASSERT_EQUAL_INT(1, r.icons.begins);
  TEST_ASSERT_EQUAL_INT(1, r.icons.children[0]->begins);
  r.pipe->renderFrame(r.canvas, 5000);
  r.pipe->renderFrame(r.canvas, 60000);
  TEST_ASSERT_EQUAL_INT(1, r.icons.begins);
  TEST_ASSERT_EQUAL_INT(1, r.icons.children[0]->begins);

  r.icons.beginOk = true;
  r.pipe->invalidateIcons();
  r.pipe->renderFrame(r.canvas, 60025);
  TEST_ASSERT_EQUAL_INT(2, r.icons.begins);
  r.pipe->renderFrame(r.canvas, 60050);
  TEST_ASSERT_EQUAL_HEX32(0xFF0000u, r.canvas.getPixel(16, 0));
  TEST_ASSERT_TRUE(r.icons.blits > 0);
}

static void test_multiple_notification_icons_clip_and_animate_independently() {
  Rig r;
  r.engine.execute(cmd(CommandType::Notify, "",
      "{\"icon\":\"legacy\",\"icons\":[{\"icon\":\"red\",\"x\":-4,\"y\":-4},"
      "{\"icon\":\"green\",\"x\":28,\"y\":4}]}"));
  r.engine.tick(0);
  r.pipe->renderFrame(r.canvas, 0);
  r.pipe->renderFrame(r.canvas, 25);
  r.pipe->renderFrame(r.canvas, 50);
  TEST_ASSERT_EQUAL_HEX32(0xFF0000u, r.canvas.getPixel(0, 0));
  TEST_ASSERT_EQUAL_HEX32(0xABCDEFu, r.canvas.getPixel(4, 0));
  TEST_ASSERT_EQUAL_HEX32(0x00FF00u, r.canvas.getPixel(31, 7));
  TEST_ASSERT_EQUAL_INT(1, r.icons.begins);
  TEST_ASSERT_EQUAL_INT(2, r.icons.creates);
  TEST_ASSERT_TRUE(r.icons.children[0] != r.icons.children[1]);
  r.pipe->renderFrame(r.canvas, 150);
  TEST_ASSERT_EQUAL_INT(150, r.icons.children[0]->lastAdvanceMs);
  TEST_ASSERT_EQUAL_INT(150, r.icons.children[1]->lastAdvanceMs);
}

static void test_position_only_updates_keep_icon_players_and_removal_releases_them() {
  Rig r;
  r.engine.execute(cmd(CommandType::SetPushedApp, "icons",
      "{\"icons\":[{\"icon\":\"red\",\"x\":0}]}"));
  r.engine.execute(switchFast("icons"));
  r.engine.tick(0);
  r.pipe->renderFrame(r.canvas, 0);
  r.engine.execute(cmd(CommandType::SetPushedApp, "icons",
      "{\"icons\":[{\"icon\":\"red\",\"x\":16}]}"));
  r.pipe->renderFrame(r.canvas, 100);
  TEST_ASSERT_EQUAL_INT(1, r.icons.creates);
  TEST_ASSERT_EQUAL_INT(1, r.icons.children[0]->begins);
  TEST_ASSERT_EQUAL_HEX32(0u, r.canvas.getPixel(0, 0));
  TEST_ASSERT_EQUAL_HEX32(0xFF0000u, r.canvas.getPixel(16, 0));
  r.engine.execute(cmd(CommandType::SetPushedApp, "icons", "{\"icons\":[]}"));
  r.pipe->renderFrame(r.canvas, 200);
  TEST_ASSERT_EQUAL_INT(1, r.icons.destroys);
  TEST_ASSERT_EQUAL_HEX32(0u, r.canvas.getPixel(16, 0));
}

static void test_positioned_icon_factory_failure_retries_without_blocking_page() {
  Rig r;
  r.icons.factoryOk = false;
  r.engine.execute(cmd(CommandType::SetPushedApp, "icons",
      "{\"backgroundColor\":\"#123456\",\"icons\":[{\"icon\":\"red\",\"x\":16}]}"));
  r.engine.execute(switchFast("icons"));
  r.engine.tick(0);
  r.pipe->renderFrame(r.canvas, 0);
  TEST_ASSERT_EQUAL_HEX32(0x123456u, r.canvas.getPixel(16, 0));
  r.icons.factoryOk = true;
  r.pipe->renderFrame(r.canvas, 4999);
  TEST_ASSERT_EQUAL_INT(0, r.icons.creates);
  r.pipe->renderFrame(r.canvas, 5000);
  TEST_ASSERT_EQUAL_INT(1, r.icons.creates);
  TEST_ASSERT_EQUAL_HEX32(0xFF0000u, r.canvas.getPixel(16, 0));
}

static void test_page_change_releases_old_icon_before_allocating_new_instances() {
  Rig r;
  r.engine.execute(cmd(CommandType::SetPushedApp, "one", "{\"icon\":\"old\"}"));
  r.engine.execute(cmd(CommandType::SetPushedApp, "two",
      "{\"icons\":[{\"icon\":\"red\",\"x\":16}]}"));
  r.engine.execute(switchFast("one"));
  r.engine.tick(0);
  r.pipe->renderFrame(r.canvas, 0);
  r.icons.factoryMinClears = r.icons.clears + 1;
  r.engine.execute(switchFast("two"));
  r.engine.tick(10);
  r.pipe->renderFrame(r.canvas, 10);
  TEST_ASSERT_EQUAL_INT(1, r.icons.creates);
  TEST_ASSERT_EQUAL_HEX32(0xFF0000u, r.canvas.getPixel(16, 0));
}

static void test_repeat_holds_a_notification_not_the_rotation() {
  Rig r;
  r.engine.execute(cmd(CommandType::Notify, "",
                       "{\"text\":\"AAAAAAAAAAAA\",\"repeat\":4,\"scroll\":\"loop\"}"));
  r.engine.tick(0);
  r.pipe->renderFrame(r.canvas, 10);
  r.pipe->renderFrame(r.canvas, 2000);
  TEST_ASSERT_TRUE_MESSAGE(r.engine.notificationHold(), "a repeating notification must hold");
  TEST_ASSERT_FALSE_MESSAGE(r.engine.rotationHold(), "it must not freeze the rotation too");
}

static void test_repeat_still_holds_the_rotation_for_a_pushed_app() {
  Rig r;
  r.engine.execute(cmd(CommandType::SetPushedApp, "a",
                       "{\"text\":\"AAAAAAAAAAAA\",\"repeat\":4,\"scroll\":\"loop\"}"));
  r.engine.tick(0);
  r.engine.execute(switchFast("a"));
  r.engine.tick(10);
  r.pipe->renderFrame(r.canvas, 10);
  r.pipe->renderFrame(r.canvas, 2000);
  TEST_ASSERT_TRUE(r.engine.rotationHold());
  TEST_ASSERT_FALSE(r.engine.notificationHold());
}

static void test_overflowing_text_is_not_held_by_the_default() {
  Rig r;
  r.engine.execute(cmd(CommandType::Notify, "", "{\"text\":\"AAAAAAAAAAAA\"}"));
  r.engine.tick(0);
  r.pipe->renderFrame(r.canvas, 10);
  r.pipe->renderFrame(r.canvas, 2000);
  TEST_ASSERT_FALSE(r.engine.notificationHold());

  Rig p;
  p.engine.execute(cmd(CommandType::SetPushedApp, "a", "{\"text\":\"AAAAAAAAAAAA\"}"));
  p.engine.tick(0);
  p.engine.execute(switchFast("a"));
  p.engine.tick(10);
  p.pipe->renderFrame(p.canvas, 10);
  p.pipe->renderFrame(p.canvas, 2000);
  TEST_ASSERT_FALSE(p.engine.rotationHold());
}

static void test_text_that_fits_is_not_held_by_repeat() {
  Rig r;
  r.engine.execute(cmd(CommandType::Notify, "", "{\"text\":\"A\",\"repeat\":1}"));
  r.engine.tick(0);
  r.pipe->renderFrame(r.canvas, 10);
  r.pipe->renderFrame(r.canvas, 2000);
  TEST_ASSERT_FALSE(r.engine.notificationHold());
}

static void test_repeat_one_opts_into_the_wait() {
  Rig r;
  r.engine.execute(cmd(CommandType::Notify, "", "{\"text\":\"AAAAAAAAAAAA\",\"repeat\":1}"));
  r.engine.tick(0);
  r.pipe->renderFrame(r.canvas, 10);
  r.pipe->renderFrame(r.canvas, 2000);
  TEST_ASSERT_TRUE(r.engine.notificationHold());

  Rig p;
  p.engine.execute(
      cmd(CommandType::SetPushedApp, "a", "{\"text\":\"AAAAAAAAAAAA\",\"repeat\":1}"));
  p.engine.tick(0);
  p.engine.execute(switchFast("a"));
  p.engine.tick(10);
  p.pipe->renderFrame(p.canvas, 10);
  p.pipe->renderFrame(p.canvas, 2000);
  TEST_ASSERT_TRUE(p.engine.rotationHold());
}

static void test_static_scroll_is_not_held_by_repeat() {
  Rig r;
  r.engine.execute(cmd(CommandType::Notify, "",
                       "{\"text\":\"AAAAAAAAAAAA\",\"scroll\":\"static\",\"repeat\":1}"));
  r.engine.tick(0);
  r.pipe->renderFrame(r.canvas, 10);
  r.pipe->renderFrame(r.canvas, 2000);
  TEST_ASSERT_FALSE(r.engine.notificationHold());
}

// A notification's sound is an alert and plays once, when the notification appears.
static void test_notification_sound_plays_once_on_appear_as_an_alert() {
  Rig r;
  r.engine.execute(
      cmd(CommandType::Notify, "", "{\"text\":\"A\",\"sound\":{\"rtttl\":\"x:d=8,o=5,b=120:c\"}}"));
  r.engine.tick(0);

  r.pipe->renderFrame(r.canvas, 0);
  r.pipe->renderFrame(r.canvas, 20);
  r.pipe->renderFrame(r.canvas, 40);
  TEST_ASSERT_EQUAL_INT(1, r.tone.plays);
  TEST_ASSERT_EQUAL_INT((int)sound::Group::Alert, (int)r.tone.groups[0]);
}

static void test_notification_speech_waits_for_appearance_and_plays_once() {
  Rig r;
  FakePcm pcm;
  pcm.voiceAvailable = true;
  r.audio.setPcm(&pcm);
  r.engine.execute(cmd(CommandType::Notify, "", R"({"text":"First","hold":true})"));
  TEST_ASSERT_EQUAL_INT((int)DispatchResult::Ok, (int)r.engine.execute(
      cmd(CommandType::Notify, "", R"({"text":"Second","sound":{"speech":"Hello world."}})")));
  runUntil(r, 0, 100);
  TEST_ASSERT_TRUE(pcm.speech.empty());
  r.engine.dismiss();
  runUntil(r, 120, 220);
  TEST_ASSERT_EQUAL_UINT(1, pcm.speech.size());
  TEST_ASSERT_EQUAL_STRING("Hello world.", pcm.speech[0].c_str());
  pcm.voicePlaying = false;
  runUntil(r, 240, 340);
  TEST_ASSERT_EQUAL_UINT(1, pcm.speech.size());
  r.audio.setPcm(nullptr);
}

// A looping sound repeats while its notification shows and stops the moment it leaves; one
// without loop plays to its end.
static void test_a_looping_notification_sound_stops_when_it_leaves() {
  Rig r;
  r.engine.execute(
      cmd(CommandType::Notify, "",
          R"({"text":"A","hold":true,"sound":{"rtttl":"x:d=8,o=5,b=120:c","loop":true}})"));
  r.tone.playing = true;
  runUntil(r, 0, 100);
  TEST_ASSERT_EQUAL_INT(1, r.tone.plays);
  r.tone.playing = false;
  r.audio.tick(1000);
  TEST_ASSERT_EQUAL_INT(2, r.tone.plays);
  const int stops = r.tone.stops;
  r.engine.dismiss();
  runUntil(r, 1100, 1200);
  TEST_ASSERT_EQUAL_INT(stops + 1, r.tone.stops);
  r.tone.playing = false;
  r.audio.tick(2000);
  TEST_ASSERT_EQUAL_INT(2, r.tone.plays);

  r.engine.execute(cmd(CommandType::Notify, "",
                       R"({"text":"B","hold":true,"sound":{"rtttl":"x:d=8,o=5,b=120:c"}})"));
  runUntil(r, 2100, 2200);
  const int playing = r.tone.stops;
  r.engine.dismiss();
  runUntil(r, 2300, 2400);
  TEST_ASSERT_EQUAL_INT(playing, r.tone.stops);
}

// Leaving stops the notification's own looping sound only: not an alert that replaced it, and not
// one that played on because the notification's own could not start.
static void test_a_leaving_notification_stops_only_its_own_loop() {
  const sound::Choices api = [] {
    sound::Choices c;
    DispatchDetail d;
    sound::parse(R"({"rtttl":"y:d=8,o=5,b=120:c","loop":true})", sound::Origin::Play, c, d);
    return c;
  }();
  Rig r;
  DispatchDetail detail;
  r.engine.execute(
      cmd(CommandType::Notify, "",
          R"({"text":"A","hold":true,"sound":{"rtttl":"x:d=8,o=5,b=120:c","loop":true}})"));
  r.tone.playing = true;
  runUntil(r, 0, 100);
  r.audio.play(api, sound::Group::Alert, "", detail);
  int stops = r.tone.stops;
  r.engine.dismiss();
  runUntil(r, 1100, 1200);
  TEST_ASSERT_EQUAL_INT(stops, r.tone.stops);
  TEST_ASSERT_TRUE(r.audio.alertPlaying());

  r.engine.execute(cmd(CommandType::Notify, "",
                       R"({"text":"B","hold":true,"sound":{"track":5,"loop":true}})"));
  runUntil(r, 1300, 1400);
  stops = r.tone.stops;
  r.engine.dismiss();
  runUntil(r, 1500, 1600);
  TEST_ASSERT_EQUAL_INT(stops, r.tone.stops);
  TEST_ASSERT_TRUE(r.audio.alertPlaying());
  r.audio.stop(sound::Stop::All);
}

// Without a voice the notification still shows; its speech finds no output.
static void test_notification_speech_without_a_voice_shows_silently() {
  Rig r;
  FakePcm pcm;
  r.audio.setPcm(&pcm);
  TEST_ASSERT_EQUAL_INT((int)DispatchResult::Ok, (int)r.engine.execute(
      cmd(CommandType::Notify, "", R"({"text":"Door","sound":{"speech":"bad"}})")));
  runUntil(r, 0, 100);
  TEST_ASSERT_EQUAL_STRING("Door", r.engine.notifications().current().text.c_str());
  TEST_ASSERT_TRUE(pcm.speech.empty());
  r.audio.setPcm(nullptr);
}

static void test_notification_speech_the_voice_refuses_keeps_the_current_notification() {
  Rig r;
  FakePcm pcm;
  pcm.voiceAvailable = true;
  r.audio.setPcm(&pcm);
  r.engine.execute(cmd(CommandType::Notify, "", R"({"text":"Keep","hold":true})"));
  r.engine.tick(0);
  TEST_ASSERT_EQUAL_INT((int)DispatchResult::ValidationError, (int)r.engine.execute(
      cmd(CommandType::Notify, "", R"({"text":"Bad","stack":false,"sound":{"speech":"bad"}})")));
  TEST_ASSERT_EQUAL_STRING("sound.speech", r.engine.lastDetail().field.c_str());
  TEST_ASSERT_EQUAL_STRING("Keep", r.engine.notifications().current().text.c_str());
  TEST_ASSERT_TRUE(pcm.speech.empty());
  r.audio.setPcm(nullptr);
}

// A script's notification finds the script's own sound before the shared one, as its own
// sound.play does.
static void test_a_scripts_notification_finds_its_own_sound_first() {
  FakePcm pcm;
  FakeFiles files;
  files.paths = {"/SCRIPTS/racer/boost.mp3", "/MP3/boost.mp3"};
  Rig r;
  r.audio.setPcm(&pcm);
  r.audio.setAssets(&files);
  DispatchDetail detail;
  TEST_ASSERT_EQUAL_INT(static_cast<int>(DispatchResult::Ok),
                        static_cast<int>(r.engine.notifyFromScript(
                            "racer", "{\"text\":\"A\",\"sound\":\"boost\"}", detail)));
  r.engine.tick(0);
  r.pipe->renderFrame(r.canvas, 0);
  TEST_ASSERT_EQUAL_size_t(1, pcm.mp3s.size());
  TEST_ASSERT_EQUAL_STRING("/SCRIPTS/racer/boost.mp3", pcm.mp3s[0].c_str());
  r.audio.setPcm(nullptr);
}

// Without its own, the script's notification falls back to /MP3; one from outside never reaches a
// script's folder.
static void test_other_notifications_play_the_shared_sound() {
  FakePcm pcm;
  FakeFiles files;
  files.paths = {"/SCRIPTS/racer/boost.mp3", "/MP3/boost.mp3", "/MP3/ding.mp3"};
  {
    Rig r;
    r.audio.setPcm(&pcm);
    r.audio.setAssets(&files);
    DispatchDetail detail;
    r.engine.notifyFromScript("racer", "{\"text\":\"A\",\"sound\":\"ding\"}", detail);
    r.engine.tick(0);
    r.pipe->renderFrame(r.canvas, 0);
    r.audio.setPcm(nullptr);
  }
  {
    Rig r;
    r.audio.setPcm(&pcm);
    r.audio.setAssets(&files);
    r.engine.execute(cmd(CommandType::Notify, "", "{\"text\":\"A\",\"sound\":\"boost\"}"));
    r.engine.tick(0);
    r.pipe->renderFrame(r.canvas, 0);
    r.audio.setPcm(nullptr);
  }
  TEST_ASSERT_EQUAL_size_t(2, pcm.mp3s.size());
  TEST_ASSERT_EQUAL_STRING("/MP3/ding.mp3", pcm.mp3s[0].c_str());
  TEST_ASSERT_EQUAL_STRING("/MP3/boost.mp3", pcm.mp3s[1].c_str());
}

static void test_repeat_holds_rotation_until_cycles_done() {
  Rig r;
  r.engine.execute(
      cmd(CommandType::SetPushedApp, "long", "{\"text\":\"AAAAAAAAAAAA\",\"repeat\":2}"));
  r.engine.execute(switchFast("long"));
  r.engine.tick(0);

  r.pipe->renderFrame(r.canvas, 0);
  TEST_ASSERT_TRUE(r.engine.rotationHold());

  for (int i = 0; i < 600; ++i) r.pipe->renderFrame(r.canvas, 20 + i * 20);
  TEST_ASSERT_FALSE(r.engine.rotationHold());
}

static int64_t runFrames(Rig& r, int64_t untilMs, bool (*done)(Rig&)) {
  for (int64_t t = 0; t <= untilMs; t += 20) {
    r.pipe->renderFrame(r.canvas, t);
    r.engine.tick(t);
    if (done(r)) return t;
  }
  return -1;
}

static void test_finished_repeats_end_a_notification_before_its_dwell() {
  Rig r;
  r.engine.execute(cmd(CommandType::Notify, "", "{\"text\":\"AAAAAAAAAAAA\",\"repeat\":1}"));
  const int64_t gone = runFrames(r, 6000, [](Rig& g) { return !g.engine.hasNotification(); });
  TEST_ASSERT_TRUE_MESSAGE(gone > 2500, "the pass must not be cut short");
  TEST_ASSERT_TRUE_MESSAGE(gone > 0 && gone < 5000, "nor must the dwell be waited out");
}

static void test_a_slow_pass_still_outlives_the_dwell() {
  Rig r;
  r.engine.execute(cmd(CommandType::Notify, "",
                       "{\"text\":\"AAAAAAAAAAAA\",\"repeat\":1,\"scroll\":{\"speed\":10}}"));
  const int64_t gone = runFrames(r, 12000, [](Rig& g) { return !g.engine.hasNotification(); });
  TEST_ASSERT_EQUAL_INT64_MESSAGE(-1, gone, "a repeating notification must outlast its dwell");
}

static void test_static_text_still_obeys_the_dwell() {
  Rig r;
  r.engine.execute(cmd(CommandType::Notify, "", "{\"text\":\"A\"}"));
  const int64_t gone = runFrames(r, 9000, [](Rig& g) { return !g.engine.hasNotification(); });
  TEST_ASSERT_TRUE_MESSAGE(gone >= 7000, "it must wait out appDurationMs");
}

static void test_a_finished_pass_does_not_carry_over_to_the_next_notification() {
  Rig r;
  r.engine.execute(cmd(CommandType::Notify, "", "{\"text\":\"AAAAAAAAAAAA\",\"repeat\":1}"));
  r.engine.execute(cmd(CommandType::Notify, "", "{\"text\":\"BBBBBBBBBBBB\",\"repeat\":1}"));
  const int64_t gone = runFrames(r, 6000, [](Rig& g) {
    return g.engine.notifications().current().text[0] == 'B';
  });
  TEST_ASSERT_TRUE(gone > 0);
  r.engine.tick(gone + 20);
  r.engine.tick(gone + 40);
  TEST_ASSERT_EQUAL_STRING("BBBBBBBBBBBB", r.engine.notifications().current().text.c_str());
}

static void test_finished_repeats_end_a_pushed_app_before_its_dwell() {
  Rig r;
  r.engine.execute(cmd(CommandType::SetPushedApp, "one",
                       "{\"text\":\"AAAAAAAAAAAA\",\"repeat\":1}"));
  r.engine.execute(cmd(CommandType::SetPushedApp, "two", "{\"text\":\"B\"}"));
  r.engine.execute(switchFast("one"));
  const int64_t left = runFrames(r, 6000, [](Rig& g) { return g.engine.appHost().inTransition(); });
  TEST_ASSERT_TRUE_MESSAGE(left > 2500, "the pass must not be cut short");
  TEST_ASSERT_TRUE_MESSAGE(left > 0 && left < 5000, "nor must the dwell be waited out");
}

struct FakeExternal : IExternalPage {
  bool on = false;
  mutable int draws = 0;
  bool active() const override { return on; }
  void draw(Canvas& canvas) const override {
    ++draws;
    canvas.fillRect(0, 0, canvas.width(), canvas.height(), 0x00FF00u);
  }
};

static void test_an_active_external_page_takes_the_place_of_the_app_rotation() {
  FakeExternal external;
  Rig r(32, 8, &external);
  r.engine.execute(cmd(CommandType::SetPushedApp, "one",
                       "{\"text\":\"\",\"backgroundColor\":\"#FF0000\"}"));
  r.engine.tick(0);
  r.engine.execute(switchFast("one"));
  r.pipe->renderFrame(r.canvas, 0);
  TEST_ASSERT_EQUAL_HEX32(0xFF0000u, r.canvas.getPixel(10, 4));
  TEST_ASSERT_EQUAL(static_cast<int>(PageKind::App), static_cast<int>(r.pipe->shownPage().kind));
  TEST_ASSERT_EQUAL_STRING("one", r.pipe->shownPage().app.c_str());

  external.on = true;
  r.pipe->renderFrame(r.canvas, 50);
  TEST_ASSERT_EQUAL_HEX32(0x00FF00u, r.canvas.getPixel(10, 4));
  TEST_ASSERT_EQUAL(static_cast<int>(PageKind::External),
                    static_cast<int>(r.pipe->shownPage().kind));
  TEST_ASSERT_TRUE(r.pipe->shownPage().app.empty());
  TEST_ASSERT_FALSE(r.engine.rotationHold());

  external.on = false;
  r.pipe->renderFrame(r.canvas, 100);
  TEST_ASSERT_EQUAL_HEX32(0xFF0000u, r.canvas.getPixel(10, 4));
  TEST_ASSERT_EQUAL_STRING("one", r.pipe->shownPage().app.c_str());
}

static void test_a_notification_interrupts_the_external_page() {
  FakeExternal external;
  external.on = true;
  Rig r(32, 8, &external);
  r.engine.execute(cmd(CommandType::Notify, "",
                       "{\"text\":\"\",\"backgroundColor\":\"#0000FF\"}"));
  r.engine.tick(0);
  TEST_ASSERT_TRUE(r.engine.hasNotification());
  const int drawsBefore = external.draws;
  r.pipe->renderFrame(r.canvas, 0);
  TEST_ASSERT_EQUAL_HEX32(0x0000FFu, r.canvas.getPixel(10, 4));
  TEST_ASSERT_EQUAL(drawsBefore, external.draws);
  TEST_ASSERT_EQUAL(static_cast<int>(PageKind::Notification),
                    static_cast<int>(r.pipe->shownPage().kind));

  r.engine.execute(cmd(CommandType::DismissNotify));
  r.engine.tick(20);
  r.pipe->renderFrame(r.canvas, 20);
  TEST_ASSERT_EQUAL_HEX32(0x00FF00u, r.canvas.getPixel(10, 4));
}

static void test_the_shown_page_names_both_apps_while_one_slides_in() {
  Rig r;
  Settings& s = r.engine.state().settings();
  s.autoTransition = false;
  s.transitionDurationMs = 1000;
  r.engine.execute(cmd(CommandType::SetPushedApp, "one", "{\"text\":\"\"}"));
  r.engine.execute(cmd(CommandType::SetPushedApp, "two", "{\"text\":\"\"}"));
  r.engine.tick(0);
  r.engine.execute(switchFast("one"));
  r.pipe->renderFrame(r.canvas, 0);
  TEST_ASSERT_TRUE(r.pipe->shownPage().incoming.empty());
  r.engine.execute(cmd(CommandType::SwitchApp, "two"));
  r.engine.tick(500);
  r.pipe->renderFrame(r.canvas, 500);
  TEST_ASSERT_EQUAL_STRING("one", r.pipe->shownPage().app.c_str());
  TEST_ASSERT_EQUAL_STRING("two", r.pipe->shownPage().incoming.c_str());
}

static void test_reverse_setting_mirrors_a_directional_transition() {
  auto renderMidSlide = [](Rig& r, int direction) {
    Settings& s = r.engine.state().settings();
    s.autoTransition = false;
    s.transitionEffect = static_cast<int>(Transition::Slide);
    s.transitionDirection = direction;
    s.transitionDurationMs = 1000;
    r.engine.execute(cmd(CommandType::SetPushedApp, "one",
                         "{\"text\":\"\",\"backgroundColor\":\"#FF0000\"}"));
    r.engine.execute(cmd(CommandType::SetPushedApp, "two",
                         "{\"text\":\"\",\"backgroundColor\":\"#0000FF\"}"));
    r.engine.tick(0);
    r.engine.execute(switchFast("one"));
    r.pipe->renderFrame(r.canvas, 0);
    r.engine.execute(cmd(CommandType::SwitchApp, "two"));
    r.engine.tick(500);
    r.pipe->renderFrame(r.canvas, 500);
  };

  Rig normal;
  renderMidSlide(normal, kTransitionNormal);
  TEST_ASSERT_EQUAL_HEX32(0xFF0000u, normal.canvas.getPixel(0, 0));
  TEST_ASSERT_EQUAL_HEX32(0x0000FFu, normal.canvas.getPixel(31, 0));

  Rig reverse;
  renderMidSlide(reverse, kTransitionReverse);
  TEST_ASSERT_EQUAL_HEX32(0x0000FFu, reverse.canvas.getPixel(0, 0));
  TEST_ASSERT_EQUAL_HEX32(0xFF0000u, reverse.canvas.getPixel(31, 0));
}

static void test_incoming_icon_decoded_during_transition() {
  Rig r;
  r.engine.execute(cmd(CommandType::SetPushedApp, "one", "{\"text\":\"A\",\"icon\":\"1\"}"));
  r.engine.execute(cmd(CommandType::SetPushedApp, "two", "{\"text\":\"A\",\"icon\":\"2\"}"));
  r.engine.execute(switchFast("one"));
  r.engine.tick(0);
  r.pipe->renderFrame(r.canvas, 0);
  TEST_ASSERT_EQUAL_STRING("1", r.icons.lastId.c_str());

  r.engine.execute(cmd(CommandType::SwitchApp, "two"));
  r.engine.tick(20);
  r.pipe->renderFrame(r.canvas, 20);
  TEST_ASSERT_EQUAL_STRING("2", r.iconsB.lastId.c_str());
  TEST_ASSERT_EQUAL_INT(1, r.iconsB.begins);
  TEST_ASSERT_TRUE(r.iconsB.blits > 0);
}

static void test_incoming_page_is_drawn_with_its_own_scroll() {
  Rig r;
  Settings& s = r.engine.state().settings();
  s.transitionEffect = static_cast<int>(Transition::Fade);
  s.transitionDurationMs = 1000;
  r.engine.execute(cmd(CommandType::SetPushedApp, "one", "{\"text\":\"AAAAAAAAAAAA\"}"));
  r.engine.execute(cmd(CommandType::SetPushedApp, "two", "{\"text\":\"AA\"}"));
  r.engine.execute(switchFast("one"));
  runUntil(r, 0, 3000);
  TEST_ASSERT_TRUE_MESSAGE(r.pipe->textX() < -5.0f, "app one must be mid-scroll");

  r.engine.execute(cmd(CommandType::SwitchApp, "two"));
  runUntil(r, 3050, 3900);
  r.engine.tick(3990);
  r.pipe->renderFrame(r.canvas, 3990);
  const int duringFirst = firstLitColumn(r.canvas);
  const int duringLit = litColumns(r.canvas);
  r.engine.tick(4100);
  r.pipe->renderFrame(r.canvas, 4100);
  TEST_ASSERT_EQUAL_INT_MESSAGE(litColumns(r.canvas), duringLit,
                                "the short text must be fully on screen while it fades in");
  TEST_ASSERT_EQUAL_INT_MESSAGE(firstLitColumn(r.canvas), duringFirst,
                                "and must not ride the outgoing scroll offset");
}

static void test_incoming_scroll_survives_the_page_change() {
  Rig r;
  Settings& s = r.engine.state().settings();
  s.transitionEffect = static_cast<int>(Transition::Fade);
  s.transitionDurationMs = 1000;
  r.engine.execute(cmd(CommandType::SetPushedApp, "one", "{\"text\":\"AAAAAAAAAAAA\"}"));
  r.engine.execute(cmd(CommandType::SetPushedApp, "two",
                       "{\"text\":\"AAAAAAAAAAAA\",\"scroll\":{\"holdMs\":0}}"));
  r.engine.execute(switchFast("one"));
  runUntil(r, 0, 3000);

  r.engine.execute(cmd(CommandType::SwitchApp, "two"));
  runUntil(r, 3050, 3990);
  const int during = firstLitColumn(r.canvas);
  r.engine.tick(4040);
  r.pipe->renderFrame(r.canvas, 4040);
  const int after = firstLitColumn(r.canvas);
  TEST_ASSERT_TRUE(during >= 0 && after >= 0);
  TEST_ASSERT_INT_WITHIN_MESSAGE(2, during, after,
                                 "the incoming text must not jump when the page settles");
  TEST_ASSERT_TRUE_MESSAGE(r.pipe->textX() < -5.0f,
                           "the scroll started during the transition must carry over");
}

static void test_incoming_icon_keeps_its_place_during_a_transition() {
  Rig r;
  r.engine.execute(cmd(CommandType::SetPushedApp, "one",
                       "{\"text\":\"AAAAAAAAAAAA\",\"icon\":\"1\",\"iconMode\":\"push\"}"));
  r.engine.execute(cmd(CommandType::SetPushedApp, "two",
                       "{\"text\":\"AAAAAAAAAAAA\",\"icon\":\"2\",\"iconMode\":\"push\"}"));
  r.engine.execute(switchFast("one"));
  runUntil(r, 0, 3000);
  TEST_ASSERT_EQUAL_INT_MESSAGE(-9, r.icons.lastBlitX, "the outgoing icon is pushed out");

  r.engine.execute(cmd(CommandType::SwitchApp, "two"));
  r.engine.tick(3050);
  r.pipe->renderFrame(r.canvas, 3050);
  TEST_ASSERT_EQUAL_INT_MESSAGE(0, r.iconsB.lastBlitX,
                                "the incoming icon must start in its column");
}

static void test_positioned_icons_survive_transition_handover_without_reopening() {
  Rig r;
  auto& settings = r.engine.state().settings();
  settings.transitionEffect = static_cast<int>(Transition::Fade);
  settings.transitionDurationMs = 1000;
  r.engine.execute(cmd(CommandType::SetPushedApp, "one",
      "{\"icon\":\"legacy\",\"icons\":[{\"icon\":\"red\",\"x\":0}]}"));
  r.engine.execute(cmd(CommandType::SetPushedApp, "two",
      "{\"icons\":[{\"icon\":\"green\",\"x\":16}]}"));
  r.engine.execute(switchFast("one"));
  runUntil(r, 0, 3000);
  TEST_ASSERT_EQUAL_INT(1, r.icons.creates);
  const int clearsBeforeHandover = r.icons.clears;
  r.engine.execute(cmd(CommandType::SwitchApp, "two"));
  runUntil(r, 3050, 4100);
  TEST_ASSERT_EQUAL_INT(1, r.iconsB.creates);
  TEST_ASSERT_EQUAL_INT(1, r.iconsB.children[0]->begins);
  TEST_ASSERT_TRUE(r.iconsB.children[0]->advances > 1);
  TEST_ASSERT_EQUAL_HEX32(0x00FF00u, r.canvas.getPixel(16, 0));
  TEST_ASSERT_EQUAL_INT(1, r.icons.destroys);
  TEST_ASSERT_EQUAL_INT(clearsBeforeHandover + 1, r.icons.clears);
  r.pipe->renderFrame(r.canvas, 4200);
  TEST_ASSERT_EQUAL_INT(clearsBeforeHandover + 1, r.icons.clears);
}

static void test_invalid_icon_lists_leave_existing_apps_and_notifications_untouched() {
  Rig r;
  TEST_ASSERT_TRUE(r.engine.execute(cmd(CommandType::SetPushedApp, "icons",
      "{\"icons\":[{\"icon\":\"red\"}]}")) == DispatchResult::Ok);
  TEST_ASSERT_TRUE(r.engine.execute(cmd(CommandType::SetPushedApp, "icons",
      "{\"icons\":[{\"icon\":\"green\",\"x\":true}]}")) == DispatchResult::ValidationError);
  TEST_ASSERT_EQUAL_STRING("icons[0].x", r.engine.lastDetail().field.c_str());
  TEST_ASSERT_EQUAL_STRING("red", r.engine.pushedApp("icons")->extras().icons[0].icon.c_str());
  TEST_ASSERT_TRUE(r.engine.execute(cmd(CommandType::Notify, "",
      "{\"icons\":[{\"icon\":\"red\"}]}")) == DispatchResult::Ok);
  TEST_ASSERT_TRUE(r.engine.execute(cmd(CommandType::Notify, "",
      "{\"stack\":false,\"icons\":[{\"icon\":\"green\",\"unknown\":0}]}")) ==
      DispatchResult::ValidationError);
  TEST_ASSERT_EQUAL_STRING("red", r.engine.notifications().current().extras().icons[0].icon.c_str());
}

static void test_builtin_app_renders_via_clock() {
  Rig r;
  TimeApp timeApp;
  r.apps.add(&timeApp);
  Settings& s = r.engine.state().settings();
  s.timeMode = 0;
  s.weekdayBar.show = false;
  s.textColor = 0xFF0000u;
  s.timeColor = OptColor{};
  r.engine.execute(switchFast("Time"));
  r.engine.tick(0);

  r.pipe->renderFrame(r.canvas, 0);
  TEST_ASSERT_EQUAL_HEX32(0xFF0000u, r.canvas.getPixel(6, 6));
}

static void test_a_pushed_app_shadows_the_builtin_of_the_same_name() {
  Rig r;
  TimeApp timeApp;
  r.apps.add(&timeApp);
  CaptureEffect fx;
  r.effects.add(&fx);
  r.engine.execute(cmd(CommandType::SetPushedApp, "Time",
                       "{\"text\":\"x\",\"effect\":\"capture\",\"effectSpeed\":3}"));
  r.engine.execute(switchFast("Time"));
  r.engine.tick(0);
  r.pipe->renderFrame(r.canvas, 0);
  TEST_ASSERT_TRUE(fx.lastSettings.hasSpeed);
}

static void test_effect_settings_reset_between_apps() {
  Rig r;
  CaptureEffect fx;
  r.effects.add(&fx);
  r.engine.execute(cmd(CommandType::SetPushedApp, "a",
                       "{\"text\":\"x\",\"effect\":\"capture\",\"effectSpeed\":3}"));
  r.engine.execute(switchFast("a"));
  r.engine.tick(0);
  r.pipe->renderFrame(r.canvas, 0);
  TEST_ASSERT_TRUE(fx.lastSettings.hasSpeed);
  r.engine.execute(cmd(CommandType::SetPushedApp, "b", "{\"text\":\"y\",\"effect\":\"capture\"}"));
  r.engine.execute(switchFast("b"));
  r.engine.tick(10);
  r.pipe->renderFrame(r.canvas, 10);
  TEST_ASSERT_FALSE(fx.lastSettings.hasSpeed);
}

static bool shows(const Canvas& c, uint32_t color, int fromX = 0) {
  for (int y = 0; y < c.height(); ++y)
    for (int x = fromX; x < c.width(); ++x)
      if (c.getPixel(x, y) == color) return true;
  return false;
}

static void test_indicators_render_and_blink() {
  Rig r;
  r.engine.tick(0);
  r.pipe->renderFrame(r.canvas, 0);
  TEST_ASSERT_FALSE(shows(r.canvas, 0x3355FFu));

  Command i1(CommandType::SetIndicator); i1.arg = 1; i1.payload = "{\"color\":\"#3355FF\"}";
  r.engine.execute(i1);
  r.pipe->renderFrame(r.canvas, 0);
  TEST_ASSERT_TRUE(shows(r.canvas, 0x3355FFu));

  Command i3(CommandType::SetIndicator);
  i3.arg = 3;
  i3.payload = "{\"color\":\"#FF0000\",\"blinkMs\":100}";
  r.engine.execute(i3);
  r.pipe->renderFrame(r.canvas, 50);
  TEST_ASSERT_TRUE(shows(r.canvas, 0xFF0000u));
  r.pipe->renderFrame(r.canvas, 150);
  TEST_ASSERT_FALSE(shows(r.canvas, 0xFF0000u));
  TEST_ASSERT_TRUE(shows(r.canvas, 0x3355FFu));
}

static void test_indicator_fade_scales_brightness() {
  Rig r;
  r.engine.tick(0);
  Command i(CommandType::SetIndicator);
  i.arg = 1;
  i.payload = "{\"color\":\"#FFFFFF\",\"fadeMs\":100}";
  r.engine.execute(i);
  r.pipe->renderFrame(r.canvas, 50);
  TEST_ASSERT_EQUAL_HEX32(0xFFFFFFu, r.canvas.getPixel(31, 0));
  r.pipe->renderFrame(r.canvas, 0);
  TEST_ASSERT_EQUAL_HEX32(0x000000u, r.canvas.getPixel(31, 0));
}

static void test_inplace_update_to_longer_text_starts_scrolling() {
  Rig r;
  r.engine.execute(cmd(CommandType::SetPushedApp, "a", "{\"text\":\"AAAA\"}"));
  r.engine.tick(0);
  r.engine.execute(switchFast("a"));
  r.engine.tick(10);
  r.pipe->renderFrame(r.canvas, 10);
  r.pipe->renderFrame(r.canvas, 3000);
  TEST_ASSERT_EQUAL_FLOAT(0.0f, r.pipe->textX());

  r.engine.execute(cmd(CommandType::SetPushedApp, "a", "{\"text\":\"AAAAAAAAAAAA\"}"));
  r.engine.tick(3010);
  r.pipe->renderFrame(r.canvas, 3010);
  TEST_ASSERT_EQUAL_FLOAT(0.0f, r.pipe->textX());
  r.pipe->renderFrame(r.canvas, 5000);
  TEST_ASSERT_TRUE_MESSAGE(r.pipe->textX() < 0.0f, "re-measured text must scroll");
}

static void test_inplace_update_with_identical_content_does_not_restart_scroll() {
  Rig r;
  r.engine.execute(cmd(CommandType::SetPushedApp, "a", "{\"text\":\"AAAAAAAAAAAA\"}"));
  r.engine.tick(0);
  r.engine.execute(switchFast("a"));
  r.engine.tick(10);
  r.pipe->renderFrame(r.canvas, 10);
  r.pipe->renderFrame(r.canvas, 3000);
  const float scrolled = r.pipe->textX();
  TEST_ASSERT_TRUE(scrolled < 0.0f);

  r.engine.execute(cmd(CommandType::SetPushedApp, "a", "{\"text\":\"AAAAAAAAAAAA\"}"));
  r.engine.tick(3010);
  r.pipe->renderFrame(r.canvas, 3010);
  TEST_ASSERT_TRUE_MESSAGE(r.pipe->textX() <= scrolled, "identical content must not rewind");
}

static void test_icon_reloads_only_when_the_icon_changes() {
  Rig r;
  r.engine.execute(cmd(CommandType::SetPushedApp, "a", "{\"text\":\"AA\",\"icon\":\"one\"}"));
  r.engine.tick(0);
  r.engine.execute(switchFast("a"));
  r.engine.tick(10);
  r.pipe->renderFrame(r.canvas, 10);
  TEST_ASSERT_EQUAL_INT(1, r.icons.begins);
  TEST_ASSERT_EQUAL_STRING("one", r.icons.lastId.c_str());

  r.engine.execute(cmd(CommandType::SetPushedApp, "a", "{\"text\":\"BB\",\"icon\":\"one\"}"));
  r.engine.tick(20);
  r.pipe->renderFrame(r.canvas, 20);
  TEST_ASSERT_EQUAL_INT(1, r.icons.begins);

  r.engine.execute(cmd(CommandType::SetPushedApp, "a", "{\"text\":\"BB\",\"icon\":\"two\"}"));
  r.engine.tick(30);
  r.pipe->renderFrame(r.canvas, 30);
  TEST_ASSERT_EQUAL_INT(2, r.icons.begins);
  TEST_ASSERT_EQUAL_STRING("two", r.icons.lastId.c_str());
}

static bool columnLit(const Canvas& c, int x) {
  for (int y = 0; y < c.height(); ++y)
    if (c.getPixel(x, y) != 0) return true;
  return false;
}

static void test_scrolling_text_keeps_a_dark_column_beside_a_fixed_icon() {
  Rig r;
  r.engine.execute(cmd(CommandType::SetPushedApp, "a",
                       "{\"text\":\"AAAAAAAAAAAA\",\"icon\":\"1\"}"));
  r.engine.execute(switchFast("a"));
  int litGapFrames = 0, textPastGapFrames = 0;
  for (int64_t t = 0; t <= 4000; t += 50) {
    r.engine.tick(t);
    r.pipe->renderFrame(r.canvas, t);
    if (columnLit(r.canvas, 8)) ++litGapFrames;
    if (r.pipe->textX() < 8.0f) ++textPastGapFrames;
  }
  TEST_ASSERT_TRUE_MESSAGE(textPastGapFrames > 0, "the text must scroll past the icon");
  TEST_ASSERT_EQUAL_INT_MESSAGE(0, litGapFrames, "column 8 must separate icon and text");
}

static void test_a_pushed_icon_keeps_the_dark_column_without_clipping_the_text() {
  Rig r;
  r.engine.execute(cmd(CommandType::SetPushedApp, "a",
                       "{\"text\":\"AAAAAAAAAAAA\",\"icon\":\"1\",\"iconMode\":\"push\"}"));
  r.engine.execute(switchFast("a"));
  int partialFrames = 0;
  for (int64_t t = 0; t <= 4000; t += 50) {
    r.engine.tick(t);
    r.pipe->renderFrame(r.canvas, t);
    const int ix = r.icons.lastBlitX;
    if (ix <= -9 || ix >= 0) continue;
    ++partialFrames;
    TEST_ASSERT_FALSE_MESSAGE(columnLit(r.canvas, ix + 8), "the gap travels with the icon");
    TEST_ASSERT_TRUE_MESSAGE(columnLit(r.canvas, ix + 9), "the text right of the gap is drawn");
  }
  TEST_ASSERT_TRUE_MESSAGE(partialFrames > 0, "the icon must be partly pushed at some point");
}

static void test_static_text_can_still_be_offset_into_the_gap_column() {
  Rig r;
  r.engine.execute(cmd(CommandType::SetPushedApp, "a",
      "{\"text\":\"A\",\"icon\":\"1\",\"textAlign\":\"start\",\"textOffsetX\":-1}"));
  r.engine.execute(switchFast("a"));
  r.engine.tick(0);
  r.pipe->renderFrame(r.canvas, 0);
  TEST_ASSERT_TRUE(columnLit(r.canvas, 8));
}

static int firstTextColumn(const Canvas& c) {
  for (int x = 0; x < c.width(); ++x)
    for (int y = 0; y < c.height(); ++y) {
      const uint32_t p = c.getPixel(x, y);
      if (p != 0 && p != 0xABCDEFu) return x;
    }
  return -1;
}

static void showPushed(Rig& r, const char* json) {
  r.engine.execute(cmd(CommandType::SetPushedApp, "a", json));
  r.engine.execute(switchFast("a"));
  r.engine.tick(0);
  r.pipe->renderFrame(r.canvas, 0);
}

static void test_icon_gap_sets_where_static_text_starts() {
  Rig wide;
  showPushed(wide, "{\"text\":\"A\",\"icon\":\"1\",\"textAlign\":\"start\",\"iconGap\":3}");
  TEST_ASSERT_EQUAL_INT(11, firstTextColumn(wide.canvas));

  Rig none;
  showPushed(none, "{\"text\":\"A\",\"icon\":\"1\",\"textAlign\":\"start\",\"iconGap\":0}");
  TEST_ASSERT_EQUAL_INT(8, firstTextColumn(none.canvas));
}

static void test_scrolling_text_never_enters_a_wider_icon_gap() {
  Rig r;
  r.engine.execute(cmd(CommandType::SetPushedApp, "a",
                       "{\"text\":\"AAAAAAAAAAAA\",\"icon\":\"1\",\"iconGap\":3}"));
  r.engine.execute(switchFast("a"));
  int litGapFrames = 0, textPastGapFrames = 0;
  for (int64_t t = 0; t <= 4000; t += 50) {
    r.engine.tick(t);
    r.pipe->renderFrame(r.canvas, t);
    if (columnLit(r.canvas, 8) || columnLit(r.canvas, 9) || columnLit(r.canvas, 10))
      ++litGapFrames;
    if (r.pipe->textX() < 11.0f && firstTextColumn(r.canvas) == 11) ++textPastGapFrames;
  }
  TEST_ASSERT_TRUE_MESSAGE(textPastGapFrames > 0, "the text must scroll past the gap");
  TEST_ASSERT_EQUAL_INT_MESSAGE(0, litGapFrames, "columns 8 to 10 must stay dark");
}

static void test_a_wide_icon_reserves_its_own_width_plus_the_gap() {
  Rig still;
  still.icons.w = 12;
  showPushed(still, "{\"text\":\"A\",\"icon\":\"1\",\"textAlign\":\"start\"}");
  TEST_ASSERT_FALSE(columnLit(still.canvas, 12));
  TEST_ASSERT_EQUAL_INT(13, firstTextColumn(still.canvas));

  Rig moving;
  moving.icons.w = 12;
  moving.engine.execute(cmd(CommandType::SetPushedApp, "a",
                            "{\"text\":\"AAAAAAAAAAAA\",\"icon\":\"1\"}"));
  moving.engine.execute(switchFast("a"));
  for (int64_t t = 0; t <= 4000; t += 50) {
    moving.engine.tick(t);
    moving.pipe->renderFrame(moving.canvas, t);
    TEST_ASSERT_FALSE_MESSAGE(columnLit(moving.canvas, 12), "column 12 is the gap");
  }
}

static void test_a_wide_icon_moves_the_progress_bar_to_its_right_edge() {
  Rig r;
  r.icons.w = 12;
  showPushed(r, "{\"icon\":\"1\",\"progress\":50,"
                "\"progressColor\":\"#FF0000\",\"progressTrackColor\":\"#0000FF\"}");
  TEST_ASSERT_TRUE(shows(r.canvas, 0xFF0000u, 12) && shows(r.canvas, 0x0000FFu, 12));
  for (int x = 0; x < 12; ++x)
    for (int y = 0; y < r.canvas.height(); ++y) {
      TEST_ASSERT_NOT_EQUAL_HEX32(0xFF0000u, r.canvas.getPixel(x, y));
      TEST_ASSERT_NOT_EQUAL_HEX32(0x0000FFu, r.canvas.getPixel(x, y));
    }
}

static void test_a_pushed_icon_carries_the_whole_gap_off_the_panel() {
  Rig r;
  r.engine.execute(cmd(CommandType::SetPushedApp, "a",
      "{\"text\":\"AAAAAAAAAAAA\",\"icon\":\"1\",\"iconMode\":\"push\",\"iconGap\":3}"));
  r.engine.execute(switchFast("a"));
  int partialFrames = 0, furthest = 0;
  for (int64_t t = 0; t <= 4000; t += 50) {
    r.engine.tick(t);
    r.pipe->renderFrame(r.canvas, t);
    const int ix = r.icons.lastBlitX;
    furthest = std::min(furthest, ix);
    if (ix <= -8 || ix >= 0) continue;
    ++partialFrames;
    for (int g = 8; g <= 10; ++g)
      TEST_ASSERT_FALSE_MESSAGE(columnLit(r.canvas, ix + g), "the gap travels with the icon");
    TEST_ASSERT_TRUE_MESSAGE(columnLit(r.canvas, ix + 11), "the text follows the gap");
  }
  TEST_ASSERT_TRUE(partialFrames > 0);
  TEST_ASSERT_EQUAL_INT(-11, furthest);
}

int main(int, char**) {
  UNITY_BEGIN();
  RUN_TEST(test_icon_gap_sets_where_static_text_starts);
  RUN_TEST(test_scrolling_text_never_enters_a_wider_icon_gap);
  RUN_TEST(test_a_wide_icon_reserves_its_own_width_plus_the_gap);
  RUN_TEST(test_a_wide_icon_moves_the_progress_bar_to_its_right_edge);
  RUN_TEST(test_a_pushed_icon_carries_the_whole_gap_off_the_panel);
  RUN_TEST(test_scrolling_text_keeps_a_dark_column_beside_a_fixed_icon);
  RUN_TEST(test_a_pushed_icon_keeps_the_dark_column_without_clipping_the_text);
  RUN_TEST(test_static_text_can_still_be_offset_into_the_gap_column);
  RUN_TEST(test_inplace_update_to_longer_text_starts_scrolling);
  RUN_TEST(test_inplace_update_with_identical_content_does_not_restart_scroll);
  RUN_TEST(test_icon_reloads_only_when_the_icon_changes);
  RUN_TEST(test_the_selected_font_decides_the_scroll_width);
  RUN_TEST(test_a_page_font_sits_by_its_own_metrics);
  RUN_TEST(test_draw_text_keeps_its_absolute_row);
  RUN_TEST(test_an_unknown_page_font_is_refused);
  RUN_TEST(test_scroll_holds_then_advances_and_resets_on_page_change);
  RUN_TEST(test_effect_frame_derived_from_wall_clock);
  RUN_TEST(test_declared_rate_scales_the_step_count);
  RUN_TEST(test_per_app_overlay_uses_the_apps_effect_settings);
  RUN_TEST(test_global_overlay_uses_the_global_settings);
  RUN_TEST(test_app_effect_settings_do_not_reach_the_global_overlay);
  RUN_TEST(test_missing_icon_uses_iconless_layout);
  RUN_TEST(test_missing_icon_frees_the_scroll_column_too);
  RUN_TEST(test_icon_is_centered_vertically);
  RUN_TEST(test_fullscreen_icon_is_background_not_column);
  RUN_TEST(test_fullscreen_icon_survives_and_text_draws_over_it);
  RUN_TEST(test_icon_decoded_once_per_page_but_advanced_every_frame);
  RUN_TEST(test_failed_icon_retries_on_timer_and_heals);
  RUN_TEST(test_panel_bounds_reach_pushed_app_and_notification_icons);
  RUN_TEST(test_an_enlarged_page_is_drawn_on_the_stage_with_its_bounds);
  RUN_TEST(test_an_enlarged_page_scrolls_by_the_stage_width);
  RUN_TEST(test_switching_the_zoom_reopens_icons_for_the_new_bounds);
  RUN_TEST(test_the_overlay_of_an_enlarged_page_covers_the_whole_panel);
  RUN_TEST(test_builtin_apps_are_not_enlarged);
  RUN_TEST(test_multiple_icons_render_in_payload_order_at_absolute_coordinates);
  RUN_TEST(test_icons_of_a_page_start_animating_on_the_same_frame);
  RUN_TEST(test_an_icon_waiting_for_memory_does_not_hold_back_the_others);
  RUN_TEST(test_additional_icons_load_one_per_frame_after_the_page_icon);
  RUN_TEST(test_missing_icons_wait_for_an_asset_change_instead_of_a_timer);
  RUN_TEST(test_multiple_notification_icons_clip_and_animate_independently);
  RUN_TEST(test_position_only_updates_keep_icon_players_and_removal_releases_them);
  RUN_TEST(test_positioned_icon_factory_failure_retries_without_blocking_page);
  RUN_TEST(test_page_change_releases_old_icon_before_allocating_new_instances);
  RUN_TEST(test_an_active_external_page_takes_the_place_of_the_app_rotation);
  RUN_TEST(test_a_notification_interrupts_the_external_page);
  RUN_TEST(test_the_shown_page_names_both_apps_while_one_slides_in);
  RUN_TEST(test_repeat_holds_a_notification_not_the_rotation);
  RUN_TEST(test_repeat_still_holds_the_rotation_for_a_pushed_app);
  RUN_TEST(test_overflowing_text_is_not_held_by_the_default);
  RUN_TEST(test_text_that_fits_is_not_held_by_repeat);
  RUN_TEST(test_repeat_one_opts_into_the_wait);
  RUN_TEST(test_static_scroll_is_not_held_by_repeat);
  RUN_TEST(test_notification_sound_plays_once_on_appear_as_an_alert);
  RUN_TEST(test_notification_speech_waits_for_appearance_and_plays_once);
  RUN_TEST(test_a_looping_notification_sound_stops_when_it_leaves);
  RUN_TEST(test_a_leaving_notification_stops_only_its_own_loop);
  RUN_TEST(test_notification_speech_without_a_voice_shows_silently);
  RUN_TEST(test_notification_speech_the_voice_refuses_keeps_the_current_notification);
  RUN_TEST(test_a_scripts_notification_finds_its_own_sound_first);
  RUN_TEST(test_other_notifications_play_the_shared_sound);
  RUN_TEST(test_repeat_holds_rotation_until_cycles_done);
  RUN_TEST(test_finished_repeats_end_a_notification_before_its_dwell);
  RUN_TEST(test_a_slow_pass_still_outlives_the_dwell);
  RUN_TEST(test_static_text_still_obeys_the_dwell);
  RUN_TEST(test_a_finished_pass_does_not_carry_over_to_the_next_notification);
  RUN_TEST(test_finished_repeats_end_a_pushed_app_before_its_dwell);
  RUN_TEST(test_reverse_setting_mirrors_a_directional_transition);
  RUN_TEST(test_incoming_icon_decoded_during_transition);
  RUN_TEST(test_incoming_page_is_drawn_with_its_own_scroll);
  RUN_TEST(test_incoming_scroll_survives_the_page_change);
  RUN_TEST(test_incoming_icon_keeps_its_place_during_a_transition);
  RUN_TEST(test_positioned_icons_survive_transition_handover_without_reopening);
  RUN_TEST(test_invalid_icon_lists_leave_existing_apps_and_notifications_untouched);
  RUN_TEST(test_builtin_app_renders_via_clock);
  RUN_TEST(test_a_pushed_app_shadows_the_builtin_of_the_same_name);
  RUN_TEST(test_effect_settings_reset_between_apps);
  RUN_TEST(test_indicators_render_and_blink);
  RUN_TEST(test_indicator_fade_scales_brightness);
  return UNITY_END();
}
