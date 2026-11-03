#include <unity.h>

#include <cstring>
#include <cstdlib>
#include <new>
#include <string>

#include "core/render/PowerAnimator.h"

using namespace awtrix;
using awtrix::render::kPowerFadeMs;
using awtrix::render::PowerAnimator;

using Phase = PowerAnimator::Phase;

// The animator holds its frame through nothrow new[]; everything else here is counted separately.
namespace {
int heldFrames = 0;
int heldAllocations = 0;
// volatile: the compiler may assume operator new reads no globals, and drop a plain store to this.
volatile bool refuseHeldFrames = false;
}

void* operator new[](std::size_t bytes, const std::nothrow_t&) noexcept {
  if (refuseHeldFrames) return nullptr;
  ++heldAllocations;
  ++heldFrames;
  return std::malloc(bytes ? bytes : 1);
}
void operator delete[](void* pointer) noexcept {
  if (pointer) --heldFrames;
  std::free(pointer);
}
void operator delete[](void* pointer, std::size_t) noexcept { operator delete[](pointer); }

void setUp() {
  heldFrames = 0;
  heldAllocations = 0;
  refuseHeldFrames = false;
}
void tearDown() {}

namespace {

constexpr int kW = 32;
constexpr int kH = 8;

uint32_t pattern(int x, int y) {
  uint32_t h = (static_cast<uint32_t>(x + 1) * 2654435761u) ^ (static_cast<uint32_t>(y + 1) * 40503u);
  h ^= h >> 15;
  return (h & 0x7F7F7Fu) | 0x030201u;
}

void fillLive(Canvas& c) {
  for (int y = 0; y < c.height(); ++y)
    for (int x = 0; x < c.width(); ++x) c.setPixel(x, y, pattern(x, y));
}

bool samePixels(const Canvas& a, const Canvas& b) {
  if (a.size() != b.size()) return false;
  return std::memcmp(a.data(), b.data(), a.size() * sizeof(uint32_t)) == 0;
}

bool allBlack(const Canvas& c) {
  for (std::size_t i = 0; i < c.size(); ++i)
    if (c.data()[i] != 0u) return false;
  return true;
}

int litPixels(const Canvas& c) {
  int n = 0;
  for (std::size_t i = 0; i < c.size(); ++i)
    if (c.data()[i] != 0u) ++n;
  return n;
}

int darkRowsFromTop(const Canvas& c, int x) {
  int n = 0;
  while (n < c.height() && c.getPixel(x, n) == 0u) ++n;
  return n;
}

int litRowsFromTop(const Canvas& c, int x) {
  int n = 0;
  while (n < c.height() && c.getPixel(x, n) != 0u) ++n;
  return n;
}

void stepFrame(PowerAnimator& a, Canvas& out, bool on, int64_t nowMs) {
  switch (a.update(on, nowMs)) {
    case Phase::Off:
      out.clear(0x000000u);
      break;
    case Phase::Out:
      a.composeOut(out);
      break;
    default:
      fillLive(out);
      a.finish(out);
      break;
  }
}

void runLive(PowerAnimator& a, Canvas& out, int64_t& nowMs, int frames) {
  for (int i = 0; i < frames; ++i) {
    stepFrame(a, out, true, nowMs);
    nowMs += 25;
  }
}

}

static void test_steady_states_are_not_busy() {
  PowerAnimator a;
  Canvas out(kW, kH);
  int64_t now = 1000;
  runLive(a, out, now, 4);
  TEST_ASSERT_TRUE(a.phase() == Phase::Live);
  TEST_ASSERT_FALSE(a.busy());

  TEST_ASSERT_TRUE(a.update(false, now) == Phase::Out);
  TEST_ASSERT_TRUE(a.busy());

  now += kPowerFadeMs;
  TEST_ASSERT_TRUE(a.update(false, now) == Phase::Off);
  TEST_ASSERT_FALSE(a.busy());
}

static void test_fade_out_starts_on_the_last_live_frame() {
  PowerAnimator a;
  Canvas out(kW, kH), live(kW, kH);
  fillLive(live);
  int64_t now = 1000;
  runLive(a, out, now, 4);
  TEST_ASSERT_TRUE(samePixels(out, live));

  stepFrame(a, out, false, now);
  TEST_ASSERT_TRUE(a.phase() == Phase::Out);
  TEST_ASSERT_TRUE(samePixels(out, live));
}

static void test_fade_out_ends_black_and_stays_black() {
  PowerAnimator a;
  Canvas out(kW, kH);
  int64_t now = 1000;
  runLive(a, out, now, 4);

  const int64_t offAt = now;
  bool sawPartial = false;
  while (now < offAt + kPowerFadeMs) {
    stepFrame(a, out, false, now);
    const int lit = litPixels(out);
    if (lit > 0 && lit < kW * kH) sawPartial = true;
    now += 25;
  }
  TEST_ASSERT_TRUE(sawPartial);

  stepFrame(a, out, false, now);
  TEST_ASSERT_TRUE(a.phase() == Phase::Off);
  TEST_ASSERT_TRUE(allBlack(out));

  for (int i = 0; i < 10; ++i) {
    now += 25;
    stepFrame(a, out, false, now);
    TEST_ASSERT_TRUE(a.phase() == Phase::Off);
    TEST_ASSERT_TRUE(allBlack(out));
  }
}

static void test_fade_out_darkens_from_the_top() {
  PowerAnimator a;
  Canvas out(kW, kH);
  int64_t now = 1000;
  runLive(a, out, now, 4);

  stepFrame(a, out, false, now);
  now += kPowerFadeMs / 2;
  stepFrame(a, out, false, now);
  TEST_ASSERT_TRUE(a.phase() == Phase::Out);

  int totalDark = 0;
  for (int x = 0; x < kW; ++x) {
    const int dark = darkRowsFromTop(out, x);
    totalDark += dark;
    for (int y = dark; y < kH; ++y)
      TEST_ASSERT_TRUE_MESSAGE(out.getPixel(x, y) != 0u,
                               ("column " + std::to_string(x)).c_str());
  }
  TEST_ASSERT_GREATER_THAN_INT(0, totalDark);
  TEST_ASSERT_LESS_THAN_INT(kW * kH, totalDark);
}

static void test_fade_in_rains_down_from_the_top() {
  PowerAnimator a;
  Canvas out(kW, kH);
  int64_t now = 1000;
  runLive(a, out, now, 2);
  stepFrame(a, out, false, now);
  now += kPowerFadeMs;
  stepFrame(a, out, false, now);
  TEST_ASSERT_TRUE(a.phase() == Phase::Off);

  stepFrame(a, out, true, now);
  TEST_ASSERT_TRUE(a.phase() == Phase::In);
  TEST_ASSERT_TRUE(allBlack(out));

  now += kPowerFadeMs / 2;
  stepFrame(a, out, true, now);
  TEST_ASSERT_TRUE(a.phase() == Phase::In);
  int totalLit = 0;
  for (int x = 0; x < kW; ++x) {
    const int lit = litRowsFromTop(out, x);
    totalLit += lit;
    for (int y = lit; y < kH; ++y)
      TEST_ASSERT_TRUE_MESSAGE(out.getPixel(x, y) == 0u,
                               ("column " + std::to_string(x)).c_str());
  }
  TEST_ASSERT_GREATER_THAN_INT(0, totalLit);
  TEST_ASSERT_LESS_THAN_INT(kW * kH, totalLit);
}

static void test_fade_in_ends_on_the_live_frame() {
  PowerAnimator a;
  Canvas out(kW, kH), live(kW, kH);
  fillLive(live);
  int64_t now = 1000;
  runLive(a, out, now, 2);
  stepFrame(a, out, false, now);
  now += kPowerFadeMs;
  stepFrame(a, out, false, now);
  TEST_ASSERT_TRUE(a.phase() == Phase::Off);

  const int64_t onAt = now;
  while (now < onAt + kPowerFadeMs) {
    stepFrame(a, out, true, now);
    now += 25;
  }
  stepFrame(a, out, true, now);
  TEST_ASSERT_TRUE(a.phase() == Phase::Live);
  TEST_ASSERT_TRUE(samePixels(out, live));
  TEST_ASSERT_FALSE(a.busy());
}

static void test_reversal_resumes_instead_of_restarting() {
  PowerAnimator a;
  Canvas out(kW, kH);
  int64_t now = 1000;
  runLive(a, out, now, 2);

  now += 10;
  stepFrame(a, out, false, now);
  now += kPowerFadeMs / 2;
  stepFrame(a, out, false, now);
  const float outProgress = a.progress();
  TEST_ASSERT_TRUE(outProgress > 0.4f && outProgress < 0.6f);

  stepFrame(a, out, true, now);
  TEST_ASSERT_TRUE(a.phase() == Phase::In);
  TEST_ASSERT_TRUE(a.progress() > 0.4f && a.progress() < 0.6f);

  now += kPowerFadeMs / 2 + 25;
  stepFrame(a, out, true, now);
  TEST_ASSERT_TRUE(a.phase() == Phase::Live);
}

static void test_immediate_reversal_settles() {
  PowerAnimator a;
  Canvas out(kW, kH);
  int64_t now = 1000;
  runLive(a, out, now, 2);

  stepFrame(a, out, false, now);
  TEST_ASSERT_TRUE(a.phase() == Phase::Out);
  stepFrame(a, out, true, now);
  now += 25;
  stepFrame(a, out, true, now);
  TEST_ASSERT_TRUE(a.phase() == Phase::Live);
  TEST_ASSERT_FALSE(a.busy());
}

static void test_coarse_and_stalled_clocks() {
  PowerAnimator a;
  Canvas out(kW, kH);
  int64_t now = 1000;
  runLive(a, out, now, 2);

  stepFrame(a, out, false, now);
  now += kPowerFadeMs * 10;
  stepFrame(a, out, false, now);
  TEST_ASSERT_TRUE(a.phase() == Phase::Off);
  TEST_ASSERT_TRUE(allBlack(out));

  PowerAnimator b;
  Canvas held(kW, kH);
  int64_t frozen = 5000;
  stepFrame(b, held, true, frozen);
  for (int i = 0; i < 20; ++i) stepFrame(b, held, false, frozen);
  TEST_ASSERT_TRUE(b.phase() == Phase::Out);
  TEST_ASSERT_TRUE(b.busy());
}

static void test_canvas_resize_is_absorbed() {
  PowerAnimator a;
  Canvas small(kW, kH);
  int64_t now = 1000;
  runLive(a, small, now, 2);

  Canvas wide(64, 16);
  now += 25;
  stepFrame(a, wide, false, now);
  TEST_ASSERT_TRUE(a.phase() == Phase::Out);
  now += kPowerFadeMs;
  stepFrame(a, wide, false, now);
  TEST_ASSERT_TRUE(a.phase() == Phase::Off);
  TEST_ASSERT_TRUE(allBlack(wide));
}

static void test_live_frames_cost_no_memory() {
  PowerAnimator a;
  Canvas out(kW, kH);
  int64_t now = 1000;
  runLive(a, out, now, 8);
  TEST_ASSERT_EQUAL_INT(0, heldAllocations);
}

static void test_a_fade_out_holds_one_frame_until_the_panel_is_dark() {
  PowerAnimator a;
  Canvas out(kW, kH);
  int64_t now = 1000;
  runLive(a, out, now, 2);

  const int64_t offAt = now;
  while (now < offAt + kPowerFadeMs) {
    stepFrame(a, out, false, now);
    TEST_ASSERT_EQUAL_INT(1, heldFrames);
    now += 25;
  }
  stepFrame(a, out, false, now);
  TEST_ASSERT_TRUE(a.phase() == Phase::Off);
  TEST_ASSERT_EQUAL_INT(1, heldAllocations);
  TEST_ASSERT_EQUAL_INT(0, heldFrames);
}

static void test_a_fade_in_holds_one_frame_until_the_panel_is_live() {
  PowerAnimator a;
  Canvas out(kW, kH);
  int64_t now = 1000;
  runLive(a, out, now, 2);
  stepFrame(a, out, false, now);
  now += kPowerFadeMs;
  stepFrame(a, out, false, now);
  heldAllocations = 0;

  const int64_t onAt = now;
  while (now < onAt + kPowerFadeMs) {
    stepFrame(a, out, true, now);
    TEST_ASSERT_EQUAL_INT(1, heldFrames);
    now += 25;
  }
  stepFrame(a, out, true, now);
  TEST_ASSERT_TRUE(a.phase() == Phase::Live);
  TEST_ASSERT_EQUAL_INT(1, heldAllocations);
  TEST_ASSERT_EQUAL_INT(0, heldFrames);
}

static void test_flipping_mid_fade_keeps_one_frame_and_continues_from_the_picture() {
  PowerAnimator a;
  Canvas out(kW, kH);
  int64_t now = 1000;
  runLive(a, out, now, 2);

  stepFrame(a, out, false, now);
  now += kPowerFadeMs / 2;
  stepFrame(a, out, false, now);
  stepFrame(a, out, true, now);
  TEST_ASSERT_TRUE(a.phase() == Phase::In);
  now += 25;
  stepFrame(a, out, true, now);
  const int litBefore = litPixels(out);
  TEST_ASSERT_GREATER_THAN_INT(0, litBefore);
  TEST_ASSERT_LESS_THAN_INT(kW * kH, litBefore);

  // Back to fading out: the part-lit picture on the panel is what falls away, not a full frame.
  stepFrame(a, out, false, now);
  TEST_ASSERT_TRUE(a.phase() == Phase::Out);
  TEST_ASSERT_LESS_OR_EQUAL_INT(litBefore, litPixels(out));
  TEST_ASSERT_EQUAL_INT(1, heldAllocations);
  TEST_ASSERT_EQUAL_INT(1, heldFrames);

  now += kPowerFadeMs;
  stepFrame(a, out, false, now);
  TEST_ASSERT_TRUE(a.phase() == Phase::Off);
  TEST_ASSERT_TRUE(allBlack(out));
  TEST_ASSERT_EQUAL_INT(0, heldFrames);
}

static void test_no_memory_for_the_fade_out_is_a_hard_cut() {
  PowerAnimator a;
  Canvas out(kW, kH);
  int64_t now = 1000;
  runLive(a, out, now, 2);

  refuseHeldFrames = true;
  stepFrame(a, out, false, now);
  TEST_ASSERT_TRUE(a.phase() == Phase::Off);
  TEST_ASSERT_FALSE(a.busy());
  TEST_ASSERT_TRUE(allBlack(out));

  // The next fade tries again and works.
  refuseHeldFrames = false;
  stepFrame(a, out, true, now);
  TEST_ASSERT_TRUE(a.phase() == Phase::In);
  TEST_ASSERT_EQUAL_INT(1, heldFrames);
}

static void test_no_memory_for_the_fade_in_is_a_hard_cut() {
  PowerAnimator a;
  Canvas out(kW, kH), live(kW, kH);
  fillLive(live);
  int64_t now = 1000;
  runLive(a, out, now, 2);
  stepFrame(a, out, false, now);
  now += kPowerFadeMs;
  stepFrame(a, out, false, now);

  refuseHeldFrames = true;
  stepFrame(a, out, true, now);
  TEST_ASSERT_TRUE(a.phase() == Phase::Live);
  TEST_ASSERT_FALSE(a.busy());
  TEST_ASSERT_TRUE(samePixels(out, live));

  // Off again with memory back: an ordinary fade.
  refuseHeldFrames = false;
  now += 25;
  stepFrame(a, out, false, now);
  TEST_ASSERT_TRUE(a.phase() == Phase::Out);
  TEST_ASSERT_TRUE(samePixels(out, live));
}

static void test_off_before_the_first_frame_goes_dark_without_fading_the_boot_screen() {
  PowerAnimator a;
  Canvas boot(kW, kH);
  fillLive(boot);
  stepFrame(a, boot, false, 1000);
  TEST_ASSERT_TRUE(a.phase() == Phase::Off);
  TEST_ASSERT_TRUE(allBlack(boot));
  TEST_ASSERT_EQUAL_INT(0, heldAllocations);
}

int main(int, char**) {
  UNITY_BEGIN();
  RUN_TEST(test_steady_states_are_not_busy);
  RUN_TEST(test_fade_out_starts_on_the_last_live_frame);
  RUN_TEST(test_fade_out_ends_black_and_stays_black);
  RUN_TEST(test_fade_out_darkens_from_the_top);
  RUN_TEST(test_fade_in_rains_down_from_the_top);
  RUN_TEST(test_fade_in_ends_on_the_live_frame);
  RUN_TEST(test_reversal_resumes_instead_of_restarting);
  RUN_TEST(test_immediate_reversal_settles);
  RUN_TEST(test_coarse_and_stalled_clocks);
  RUN_TEST(test_canvas_resize_is_absorbed);
  RUN_TEST(test_live_frames_cost_no_memory);
  RUN_TEST(test_a_fade_out_holds_one_frame_until_the_panel_is_dark);
  RUN_TEST(test_a_fade_in_holds_one_frame_until_the_panel_is_live);
  RUN_TEST(test_flipping_mid_fade_keeps_one_frame_and_continues_from_the_picture);
  RUN_TEST(test_no_memory_for_the_fade_out_is_a_hard_cut);
  RUN_TEST(test_no_memory_for_the_fade_in_is_a_hard_cut);
  RUN_TEST(test_off_before_the_first_frame_goes_dark_without_fading_the_boot_screen);
  return UNITY_END();
}
