#include <unity.h>

#include <cstring>
#include <string>

#include "core/render/PowerAnimator.h"

using namespace awtrix;
using awtrix::render::kPowerFadeMs;
using awtrix::render::PowerAnimator;

using Phase = PowerAnimator::Phase;

void setUp() {}
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
  PowerAnimator a(kW, kH);
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
  PowerAnimator a(kW, kH);
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
  PowerAnimator a(kW, kH);
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
  PowerAnimator a(kW, kH);
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
  PowerAnimator a(kW, kH);
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
  PowerAnimator a(kW, kH);
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
  PowerAnimator a(kW, kH);
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
  PowerAnimator a(kW, kH);
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
  PowerAnimator a(kW, kH);
  Canvas out(kW, kH);
  int64_t now = 1000;
  runLive(a, out, now, 2);

  stepFrame(a, out, false, now);
  now += kPowerFadeMs * 10;
  stepFrame(a, out, false, now);
  TEST_ASSERT_TRUE(a.phase() == Phase::Off);
  TEST_ASSERT_TRUE(allBlack(out));

  PowerAnimator b(kW, kH);
  Canvas held(kW, kH);
  int64_t frozen = 5000;
  stepFrame(b, held, true, frozen);
  for (int i = 0; i < 20; ++i) stepFrame(b, held, false, frozen);
  TEST_ASSERT_TRUE(b.phase() == Phase::Out);
  TEST_ASSERT_TRUE(b.busy());
}

static void test_canvas_resize_is_absorbed() {
  PowerAnimator a(kW, kH);
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
  return UNITY_END();
}
