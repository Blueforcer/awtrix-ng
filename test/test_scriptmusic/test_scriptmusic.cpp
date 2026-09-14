#include <unity.h>

#include <string>

#include "core/RuntimeState.h"
#include "core/apps/IApp.h"
#include "core/audio/AudioStats.h"
#include "core/render/Canvas.h"
#include "core/script/BerryVM.h"
#include "core/script/ScriptBindings.h"
#include "core/script/ScriptServices.h"

using namespace awtrix;

static script::ScriptServices g_svc;
static RuntimeState g_rt;
static std::string g_log;
static audio::FrameStats g_stats;
static bool g_fresh = true;
static int g_calls = 0;

void setUp() {
  g_rt = RuntimeState{};
  g_log.clear();
  g_stats = audio::FrameStats{};
  g_fresh = true;
  g_calls = 0;
  g_svc = script::ScriptServices{};
  g_svc.log = [](const std::string& s) { g_log += s; };
  g_svc.audioStats = [](int64_t, audio::FrameStats& out) {
    ++g_calls;
    if (!g_fresh) return false;
    out = g_stats;
    return true;
  };
  script::setServices(&g_svc);
}
void tearDown() { script::setServices(nullptr); }

static void run(const char* body, int64_t nowMs = 0, Canvas* canvas = nullptr) {
  script::BerryVM vm;
  std::string err;
  TEST_ASSERT_TRUE_MESSAGE(script::installBindings(vm, err), err.c_str());
  const std::string src = std::string("def draw() ") + body + " end";
  TEST_ASSERT_TRUE_MESSAGE(vm.load(src.c_str()), vm.lastError().c_str());
  RenderCtx ctx;
  ctx.runtime = &g_rt;
  ctx.nowMs = nowMs;
  script::BindingScope s(canvas, &ctx, "T");
  TEST_ASSERT_TRUE_MESSAGE(vm.call("draw"), vm.lastError().c_str());
}

static bool logged(const char* needle) { return g_log.find(needle) != std::string::npos; }

static const char* kAll =
    "log(str(size(music.bands(4))) + '/' + str(music.bands(4)) + '/' + str(music.level()) + '/' + "
    "str(music.beat()) + '/' + str(music.playing()))";

static void test_without_a_service_everything_is_zero_never_nil() {
  g_svc.audioStats = nullptr;
  run(kAll);
  TEST_ASSERT_TRUE(logged("4/[0, 0, 0, 0]/0/false/false"));
}

static void test_n_is_clamped() {
  run("log(str(size(music.bands())) + '/' + str(size(music.bands(0))) + '/' + "
      "str(size(music.bands(99))) + '/' + str(size(music.bands(1))))");
  TEST_ASSERT_TRUE(logged("32/32/32/1"));
}

static void test_bands_merge_by_max_and_scale() {
  g_stats.bands[0] = 255;
  g_stats.bands[1] = 10;
  g_stats.bands[2] = 127;
  g_stats.bands[3] = 0;
  run("var b = music.bands(16, 8) log(str(b[0]) + '/' + str(b[1]) + '/' + "
      "str(music.bands(32)[2]) + '/' + str(music.bands(16)[1]))");
  TEST_ASSERT_TRUE(logged("8/4/127/127"));
}

static void test_stale_service_zeroes_everything() {
  g_stats.bands[0] = 200;
  g_stats.level = 200;
  g_stats.beat = true;
  g_fresh = false;
  run(kAll);
  TEST_ASSERT_TRUE(logged("4/[0, 0, 0, 0]/0/false/false"));
}

static void test_beat_level_and_active_pass_through() {
  g_stats.level = 200;
  g_stats.beat = true;
  g_rt.radioPlaying = true;
  run(kAll);
  TEST_ASSERT_TRUE(logged("4/[0, 0, 0, 0]/200/true/true"));
}

static void test_active_follows_mp3_playback_too() {
  g_rt.mp3Playing = true;
  run("log(str(music.playing()))");
  TEST_ASSERT_TRUE(logged("true"));
}

static void test_one_fetch_per_frame() {
  run(kAll, 0);
  TEST_ASSERT_EQUAL_INT(1, g_calls);
  run(kAll, 24);
  TEST_ASSERT_EQUAL_INT(2, g_calls);
  run(kAll, 24);
  TEST_ASSERT_EQUAL_INT(2, g_calls);
}

static void test_bar_chart_takes_bands_directly() {
  g_stats.bands[0] = 255;
  g_stats.bands[1] = 255;
  Canvas c(32, 8);
  run("bar_chart(music.bands(16, 8), 0x00FF00, false)", 0, &c);
  TEST_ASSERT_EQUAL_HEX32(0x00FF00u, c.getPixel(0, 0));
  TEST_ASSERT_EQUAL_HEX32(0x00FF00u, c.getPixel(0, 7));
  TEST_ASSERT_EQUAL_HEX32(0u, c.getPixel(2, 7));
}

int main(int, char**) {
  UNITY_BEGIN();
  RUN_TEST(test_without_a_service_everything_is_zero_never_nil);
  RUN_TEST(test_n_is_clamped);
  RUN_TEST(test_bands_merge_by_max_and_scale);
  RUN_TEST(test_stale_service_zeroes_everything);
  RUN_TEST(test_beat_level_and_active_pass_through);
  RUN_TEST(test_active_follows_mp3_playback_too);
  RUN_TEST(test_one_fetch_per_frame);
  RUN_TEST(test_bar_chart_takes_bands_directly);
  return UNITY_END();
}
