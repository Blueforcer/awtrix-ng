#include <unity.h>

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

struct FSound : ISoundService {
  bool playSound(const std::string&) override { return true; }
  void playRtttl(const std::string&) override {}
  void r2d2(const std::string&) override {}
  void stop() override {}
};
struct FDisplay : IDisplayService {
  void sendScreen() override {}
};
struct FSystem : ISystemService {
  void reboot() override {}
  void sleep(uint64_t) override {}
  void factoryReset() override {}
  void resetSettings() override {}
};

struct FakeIcon : IPageIcon {
  int begins = 0, clears = 0, advances = 0, blits = 0;
  bool beginOk = true;
  int w = 8;
  int lastBlitX = -1;
  std::string lastId;
  bool begin(const std::string& id) override {
    ++begins;
    lastId = id;
    return beginOk;
  }
  void clear() override { ++clears; }
  void advance(int64_t) override { ++advances; }
  void blit(Canvas& dst, int xOffset) const override {
    auto* self = const_cast<FakeIcon*>(this);
    self->blits++;
    self->lastBlitX = xOffset;
    for (int y = 0; y < 8; ++y)
      for (int x = 0; x < w; ++x) dst.setPixel(x + xOffset, y, 0xABCDEFu);
  }
  int width() const override { return w; }
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

struct FakeSound : IPageSound {
  int plays = 0;
  bool playing = false;
  void play(const AppSpec&) override { ++plays; }
  bool isPlaying() const override { return playing; }
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

struct Rig {
  FSound so; FDisplay di; FSystem sy;
  CoreEngine engine{so, di, sy};
  AppRegistry apps;
  EffectRegistry effects, overlays;
  FakeIcon icons;
  FakeSound sound;
  FakeClock clock;
  Canvas canvas{32, 8};
  FakeIcon iconsB;
  RenderPipeline* pipe = nullptr;

  Rig() {
    RenderPipelineDeps d;
    d.engine = &engine;
    d.apps = &apps;
    d.effects = &effects;
    d.overlays = &overlays;
    d.fonts[0] = &kFont;
    d.fonts[1] = &kWideFont;
    d.icons = &icons;
    d.iconsB = &iconsB;
    d.sound = &sound;
    d.clock = &clock;
    pipe = new RenderPipeline(32, 8, d);
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

static void test_notification_sound_plays_once_on_appear() {
  Rig r;
  r.engine.execute(cmd(CommandType::Notify, "", "{\"text\":\"A\",\"soundRtttl\":\"x:d=8,o=5,b=120:c\"}"));
  r.engine.tick(0);

  r.pipe->renderFrame(r.canvas, 0);
  r.pipe->renderFrame(r.canvas, 20);
  r.pipe->renderFrame(r.canvas, 40);
  TEST_ASSERT_EQUAL_INT(1, r.sound.plays);
}

static void test_loopsound_retriggers_only_when_finished() {
  Rig r;
  r.engine.execute(
      cmd(CommandType::Notify, "", "{\"text\":\"A\",\"soundRtttl\":\"x:d=8,o=5,b=120:c\",\"soundLoop\":true}"));
  r.engine.tick(0);

  r.sound.playing = true;
  r.pipe->renderFrame(r.canvas, 0);
  TEST_ASSERT_EQUAL_INT(1, r.sound.plays);
  r.pipe->renderFrame(r.canvas, 20);
  TEST_ASSERT_EQUAL_INT(1, r.sound.plays);

  r.sound.playing = false;
  r.pipe->renderFrame(r.canvas, 40);
  TEST_ASSERT_EQUAL_INT(2, r.sound.plays);
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

static void test_indicators_render_and_blink() {
  Rig r;
  r.engine.tick(0);
  r.pipe->renderFrame(r.canvas, 0);
  TEST_ASSERT_EQUAL_HEX32(0x000000u, r.canvas.getPixel(31, 0));
  TEST_ASSERT_EQUAL_HEX32(0x000000u, r.canvas.getPixel(31, 7));

  Command i1(CommandType::SetIndicator); i1.arg = 1; i1.payload = "{\"color\":\"#3355FF\"}";
  r.engine.execute(i1);
  r.pipe->renderFrame(r.canvas, 0);
  TEST_ASSERT_EQUAL_HEX32(0x3355FFu, r.canvas.getPixel(31, 0));
  TEST_ASSERT_EQUAL_HEX32(0x000000u, r.canvas.getPixel(31, 7));

  Command i3(CommandType::SetIndicator);
  i3.arg = 3;
  i3.payload = "{\"color\":\"#FF0000\",\"blinkMs\":100}";
  r.engine.execute(i3);
  r.pipe->renderFrame(r.canvas, 50);
  TEST_ASSERT_EQUAL_HEX32(0xFF0000u, r.canvas.getPixel(31, 7));
  r.pipe->renderFrame(r.canvas, 150);
  TEST_ASSERT_EQUAL_HEX32(0x000000u, r.canvas.getPixel(31, 7));
}

static void test_indicators_have_the_upstream_corner_shapes() {
  Rig r;
  r.engine.tick(0);
  for (int id = 1; id <= 3; ++id) {
    Command i(CommandType::SetIndicator);
    i.arg = id;
    i.payload = "{\"color\":\"#FFFFFF\"}";
    r.engine.execute(i);
  }
  r.pipe->renderFrame(r.canvas, 0);

  const int lit[8][2] = {{31, 0}, {30, 0}, {31, 1}, {31, 3}, {31, 4}, {31, 7}, {31, 6}, {30, 7}};
  for (const auto& p : lit)
    TEST_ASSERT_EQUAL_HEX32(0xFFFFFFu, r.canvas.getPixel(p[0], p[1]));
  TEST_ASSERT_EQUAL_HEX32(0x000000u, r.canvas.getPixel(30, 1));
  TEST_ASSERT_EQUAL_HEX32(0x000000u, r.canvas.getPixel(31, 2));
  TEST_ASSERT_EQUAL_HEX32(0x000000u, r.canvas.getPixel(31, 5));
  TEST_ASSERT_EQUAL_HEX32(0x000000u, r.canvas.getPixel(30, 6));
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

int main(int, char**) {
  UNITY_BEGIN();
  RUN_TEST(test_inplace_update_to_longer_text_starts_scrolling);
  RUN_TEST(test_inplace_update_with_identical_content_does_not_restart_scroll);
  RUN_TEST(test_icon_reloads_only_when_the_icon_changes);
  RUN_TEST(test_the_selected_font_decides_the_scroll_width);
  RUN_TEST(test_scroll_holds_then_advances_and_resets_on_page_change);
  RUN_TEST(test_effect_frame_derived_from_wall_clock);
  RUN_TEST(test_declared_rate_scales_the_step_count);
  RUN_TEST(test_per_app_overlay_uses_the_apps_effect_settings);
  RUN_TEST(test_global_overlay_uses_the_global_settings);
  RUN_TEST(test_app_effect_settings_do_not_reach_the_global_overlay);
  RUN_TEST(test_missing_icon_uses_iconless_layout);
  RUN_TEST(test_missing_icon_frees_the_scroll_column_too);
  RUN_TEST(test_fullscreen_icon_is_background_not_column);
  RUN_TEST(test_fullscreen_icon_survives_and_text_draws_over_it);
  RUN_TEST(test_icon_decoded_once_per_page_but_advanced_every_frame);
  RUN_TEST(test_failed_icon_retries_on_timer_and_heals);
  RUN_TEST(test_repeat_holds_a_notification_not_the_rotation);
  RUN_TEST(test_repeat_still_holds_the_rotation_for_a_pushed_app);
  RUN_TEST(test_overflowing_text_is_not_held_by_the_default);
  RUN_TEST(test_text_that_fits_is_not_held_by_repeat);
  RUN_TEST(test_repeat_one_opts_into_the_wait);
  RUN_TEST(test_static_scroll_is_not_held_by_repeat);
  RUN_TEST(test_notification_sound_plays_once_on_appear);
  RUN_TEST(test_loopsound_retriggers_only_when_finished);
  RUN_TEST(test_repeat_holds_rotation_until_cycles_done);
  RUN_TEST(test_finished_repeats_end_a_notification_before_its_dwell);
  RUN_TEST(test_a_slow_pass_still_outlives_the_dwell);
  RUN_TEST(test_static_text_still_obeys_the_dwell);
  RUN_TEST(test_a_finished_pass_does_not_carry_over_to_the_next_notification);
  RUN_TEST(test_finished_repeats_end_a_pushed_app_before_its_dwell);
  RUN_TEST(test_incoming_icon_decoded_during_transition);
  RUN_TEST(test_incoming_page_is_drawn_with_its_own_scroll);
  RUN_TEST(test_incoming_scroll_survives_the_page_change);
  RUN_TEST(test_incoming_icon_keeps_its_place_during_a_transition);
  RUN_TEST(test_builtin_app_renders_via_clock);
  RUN_TEST(test_effect_settings_reset_between_apps);
  RUN_TEST(test_indicators_render_and_blink);
  RUN_TEST(test_indicators_have_the_upstream_corner_shapes);
  RUN_TEST(test_indicator_fade_scales_brightness);
  return UNITY_END();
}
