#include <unity.h>

#include <cstring>
#include <set>
#include <string>
#include <vector>

#include "core/Transitions.h"
#include "core/render/TransitionComposer.h"

using namespace awtrix;
using awtrix::render::composeTransition;
using awtrix::render::resolveTransition;

void setUp() {}
void tearDown() {}

namespace {

std::vector<Transition> allEffects() {
  std::vector<Transition> v;
  for (int i = 1; i < static_cast<int>(Transition::Count); ++i) v.push_back(static_cast<Transition>(i));
  return v;
}

const char* name(Transition t) { return kTransitionNames[static_cast<int>(t)]; }

uint32_t pattern(uint32_t salt, int x, int y) {
  uint32_t h = (static_cast<uint32_t>(x + 1) * 2654435761u) ^
               (static_cast<uint32_t>(y + 1) * 40503u) ^ (salt * 2246822519u);
  h ^= h >> 15;
  return (h & 0x7F7F7Fu) | 0x030201u;
}

void fillPattern(Canvas& c, uint32_t salt) {
  for (int y = 0; y < c.height(); ++y)
    for (int x = 0; x < c.width(); ++x) c.setPixel(x, y, pattern(salt, x, y));
}

bool samePixels(const Canvas& a, const Canvas& b) {
  if (a.size() != b.size()) return false;
  return std::memcmp(a.data(), b.data(), a.size() * sizeof(uint32_t)) == 0;
}

float blueShare(const Canvas& c) {
  int blue = 0;
  for (int y = 0; y < c.height(); ++y)
    for (int x = 0; x < c.width(); ++x) {
      const uint32_t p = c.getPixel(x, y);
      if ((p & 0xFFu) > ((p >> 16) & 0xFFu)) ++blue;
    }
  return static_cast<float>(blue) / static_cast<float>(c.size());
}

struct Geometry {
  int w, h;
};

const Geometry kGeometries[] = {{8, 8}, {32, 8}, {64, 8}, {128, 8}, {32, 16}};

std::string where(Transition t, const Geometry& g) {
  return std::string(name(t)) + " at " + std::to_string(g.w) + "x" + std::to_string(g.h);
}

}

static void test_named_effect_passes_through() {
  for (Transition t : allEffects())
    TEST_ASSERT_EQUAL_INT(static_cast<int>(t), static_cast<int>(resolveTransition(static_cast<int>(t), 12345u)));
}

static void test_random_and_out_of_range_resolve_to_a_real_effect() {
  const int last = static_cast<int>(Transition::Count) - 1;
  for (uint32_t seed = 0; seed < 512; ++seed) {
    for (int stored : {0, -1, -99, static_cast<int>(Transition::Count), 9999}) {
      const int got = static_cast<int>(resolveTransition(stored, seed));
      TEST_ASSERT_GREATER_OR_EQUAL_INT(1, got);
      TEST_ASSERT_LESS_OR_EQUAL_INT(last, got);
    }
  }
}

static void test_random_reaches_every_effect() {
  std::set<int> seen;
  for (uint32_t seed = 0; seed < 4096; ++seed) seen.insert(static_cast<int>(resolveTransition(0, seed)));
  TEST_ASSERT_EQUAL_size_t(kTransitionCount - 1, seen.size());
}

static void test_progress_zero_is_the_old_page() {
  for (const Geometry& g : kGeometries) {
    Canvas from(g.w, g.h), to(g.w, g.h), out(g.w, g.h);
    fillPattern(from, 1u);
    fillPattern(to, 2u);
    for (Transition t : allEffects()) {
      composeTransition(out, from, to, t, 0.0f, 1);
      TEST_ASSERT_TRUE_MESSAGE(samePixels(out, from), where(t, g).c_str());
      composeTransition(out, from, to, t, -0.5f, 1);
      TEST_ASSERT_TRUE_MESSAGE(samePixels(out, from), where(t, g).c_str());
    }
  }
}

static void test_progress_one_is_the_new_page() {
  for (const Geometry& g : kGeometries) {
    Canvas from(g.w, g.h), to(g.w, g.h), out(g.w, g.h);
    fillPattern(from, 1u);
    fillPattern(to, 2u);
    for (Transition t : allEffects()) {
      composeTransition(out, from, to, t, 1.0f, 1);
      TEST_ASSERT_TRUE_MESSAGE(samePixels(out, to), where(t, g).c_str());
      composeTransition(out, from, to, t, 2.0f, -1);
      TEST_ASSERT_TRUE_MESSAGE(samePixels(out, to), where(t, g).c_str());
    }
  }
}

static void test_every_effect_travels_from_old_to_new() {
  for (const Geometry& g : kGeometries) {
    Canvas from(g.w, g.h), to(g.w, g.h), out(g.w, g.h);
    from.clear(0xFF0000u);
    to.clear(0x0000FFu);
    for (Transition t : allEffects()) {
      composeTransition(out, from, to, t, 0.02f, 1);
      const float early = blueShare(out);
      composeTransition(out, from, to, t, 0.98f, 1);
      const float late = blueShare(out);
      const std::string w = where(t, g);
      TEST_ASSERT_TRUE_MESSAGE(early <= 0.4f, w.c_str());
      TEST_ASSERT_TRUE_MESSAGE(late >= 0.6f, w.c_str());
    }
  }
}

static void test_effects_are_visually_distinct_mid_transition() {
  const Geometry g = {32, 8};
  Canvas from(g.w, g.h), to(g.w, g.h);
  fillPattern(from, 1u);
  fillPattern(to, 2u);

  std::vector<Transition> effects = allEffects();
  std::vector<Canvas> frames;
  for (Transition t : effects) {
    Canvas out(g.w, g.h);
    composeTransition(out, from, to, t, 0.45f, 1);
    frames.push_back(out);
  }
  for (std::size_t a = 0; a < frames.size(); ++a) {
    TEST_ASSERT_FALSE_MESSAGE(samePixels(frames[a], from), name(effects[a]));
    TEST_ASSERT_FALSE_MESSAGE(samePixels(frames[a], to), name(effects[a]));
    for (std::size_t b = a + 1; b < frames.size(); ++b) {
      const std::string msg = std::string(name(effects[a])) + " == " + name(effects[b]);
      TEST_ASSERT_FALSE_MESSAGE(samePixels(frames[a], frames[b]), msg.c_str());
    }
  }
}

static void test_column_staggered_effects_are_ragged() {
  const Transition staggered[] = {Transition::Rain, Transition::Melt};
  for (const Geometry& g : kGeometries) {
    if (g.w < 16) continue;
    Canvas from(g.w, g.h), to(g.w, g.h), out(g.w, g.h);
    from.clear(0xFF0000u);
    to.clear(0x0000FFu);
    for (Transition t : staggered) {
      composeTransition(out, from, to, t, 0.45f, 1);
      std::set<int> fronts;
      for (int x = 0; x < g.w; ++x) {
        int rows = 0;
        for (int y = 0; y < g.h; ++y)
          if ((out.getPixel(x, y) & 0xFFu) > 0) ++rows;
        fronts.insert(rows);
      }
      TEST_ASSERT_GREATER_THAN_size_t_MESSAGE(2, fronts.size(), where(t, g).c_str());
    }
  }
}

static void test_pacing_table_covers_every_effect() {
  for (int i = 0; i < static_cast<int>(Transition::Count); ++i) {
    const Pacing pc = transitionPacing(static_cast<Transition>(i));
    TEST_ASSERT_TRUE(pc == Pacing::Eased || pc == Pacing::Linear);
  }
  TEST_ASSERT_TRUE(transitionPacing(static_cast<Transition>(-1)) == Pacing::Eased);
  TEST_ASSERT_TRUE(transitionPacing(Transition::Count) == Pacing::Eased);
}

static void test_eased_effects_start_gently() {
  const Geometry g = {32, 8};
  Canvas from(g.w, g.h), to(g.w, g.h), early(g.w, g.h), late(g.w, g.h);
  from.clear(0xFF0000u);
  to.clear(0x0000FFu);

  composeTransition(early, from, to, Transition::Slide, 0.10f, 1);
  composeTransition(late, from, to, Transition::Slide, 0.90f, 1);
  const float e = blueShare(early), l = blueShare(late);
  TEST_ASSERT_TRUE(e < 0.10f);
  TEST_ASSERT_TRUE(l > 0.90f);

  Canvas mid(g.w, g.h);
  composeTransition(mid, from, to, Transition::Slide, 0.25f, 1);
  const float m = blueShare(mid);
  TEST_ASSERT_TRUE(m > 0.0f);
  TEST_ASSERT_TRUE(m < 0.25f);

  Canvas lin(g.w, g.h);
  composeTransition(lin, from, to, Transition::Pixelate, 0.25f, 1);
  TEST_ASSERT_TRUE(blueShare(lin) > m);
}

static void test_blink_and_flash_have_no_dead_zone() {
  const Geometry g = {32, 8};
  Canvas from(g.w, g.h), to(g.w, g.h), out(g.w, g.h);
  fillPattern(from, 1u);
  fillPattern(to, 2u);
  for (Transition t : {Transition::Blink, Transition::Flash}) {
    composeTransition(out, from, to, t, 0.25f, 1);
    TEST_ASSERT_FALSE_MESSAGE(samePixels(out, from), name(t));
    composeTransition(out, from, to, t, 0.75f, 1);
    TEST_ASSERT_FALSE_MESSAGE(samePixels(out, to), name(t));
  }
}

static void test_staggered_lanes_share_one_speed() {
  const Geometry g = {32, 8};
  for (Transition t : {Transition::Rain, Transition::Melt}) {
    Canvas from(g.w, g.h), to(g.w, g.h), a(g.w, g.h), b(g.w, g.h);
    from.clear(0xFF0000u);
    to.clear(0x0000FFu);
    composeTransition(a, from, to, t, 0.55f, 1);
    composeTransition(b, from, to, t, 0.70f, 1);
    auto blueRows = [](const Canvas& c, int x) {
      int n = 0;
      for (int y = 0; y < c.height(); ++y)
        if ((c.getPixel(x, y) & 0xFFu) > 0) ++n;
      return n;
    };
    std::set<int> advances;
    for (int x = 0; x < g.w; ++x) {
      const int d = blueRows(b, x) - blueRows(a, x);
      if (blueRows(a, x) > 0 && blueRows(b, x) < g.h) advances.insert(d);
    }
    TEST_ASSERT_TRUE_MESSAGE(advances.size() <= 2u, name(t));
  }
}

static void test_direction_aware_effects_mirror() {
  const Transition directional[] = {Transition::Slide, Transition::Rotate,    Transition::Cover,
                                    Transition::Uncover, Transition::Wave,    Transition::Rain,
                                    Transition::Melt,  Transition::Interlace};
  for (const Geometry& g : kGeometries) {
    Canvas from(g.w, g.h), to(g.w, g.h), fwd(g.w, g.h), back(g.w, g.h);
    fillPattern(from, 1u);
    fillPattern(to, 2u);
    for (Transition t : directional) {
      composeTransition(fwd, from, to, t, 0.45f, 1);
      composeTransition(back, from, to, t, 0.45f, -1);
      TEST_ASSERT_FALSE_MESSAGE(samePixels(fwd, back), where(t, g).c_str());
    }
  }
}

static void test_symmetric_effects_ignore_direction() {
  const Transition symmetric[] = {Transition::Dim,   Transition::Zoom,   Transition::Pixelate,
                                  Transition::Curtain, Transition::Ripple, Transition::Blink,
                                  Transition::Reload, Transition::Fade,  Transition::Split,
                                  Transition::Blinds, Transition::Blocks, Transition::Flash,
                                  Transition::Diamond};
  const Geometry g = {32, 8};
  Canvas from(g.w, g.h), to(g.w, g.h), fwd(g.w, g.h), back(g.w, g.h);
  fillPattern(from, 1u);
  fillPattern(to, 2u);
  for (Transition t : symmetric) {
    composeTransition(fwd, from, to, t, 0.45f, 1);
    composeTransition(back, from, to, t, 0.45f, -1);
    TEST_ASSERT_TRUE_MESSAGE(samePixels(fwd, back), name(t));
  }
}

static void test_names_and_enum_agree() {
  TEST_ASSERT_EQUAL_size_t(static_cast<std::size_t>(Transition::Count), kTransitionCount);
  TEST_ASSERT_EQUAL_STRING("Random", kTransitionNames[0]);
  TEST_ASSERT_EQUAL_STRING("Slide", name(Transition::Slide));
  TEST_ASSERT_EQUAL_STRING("Fade", name(Transition::Fade));
  TEST_ASSERT_EQUAL_STRING("Interlace", name(Transition::Interlace));
  std::set<std::string> unique;
  for (std::size_t i = 0; i < kTransitionCount; ++i) unique.insert(kTransitionNames[i]);
  TEST_ASSERT_EQUAL_size_t(kTransitionCount, unique.size());
}

static void test_upstream_indices_are_stable() {
  const char* upstream[] = {"Random", "Slide",   "Dim",    "Zoom",  "Rotate", "Pixelate",
                            "Curtain", "Ripple", "Blink",  "Reload", "Fade"};
  for (std::size_t i = 0; i < sizeof(upstream) / sizeof(upstream[0]); ++i)
    TEST_ASSERT_EQUAL_STRING(upstream[i], kTransitionNames[i]);
}

int main(int, char**) {
  UNITY_BEGIN();
  RUN_TEST(test_named_effect_passes_through);
  RUN_TEST(test_random_and_out_of_range_resolve_to_a_real_effect);
  RUN_TEST(test_random_reaches_every_effect);
  RUN_TEST(test_progress_zero_is_the_old_page);
  RUN_TEST(test_progress_one_is_the_new_page);
  RUN_TEST(test_every_effect_travels_from_old_to_new);
  RUN_TEST(test_effects_are_visually_distinct_mid_transition);
  RUN_TEST(test_column_staggered_effects_are_ragged);
  RUN_TEST(test_pacing_table_covers_every_effect);
  RUN_TEST(test_eased_effects_start_gently);
  RUN_TEST(test_blink_and_flash_have_no_dead_zone);
  RUN_TEST(test_staggered_lanes_share_one_speed);
  RUN_TEST(test_direction_aware_effects_mirror);
  RUN_TEST(test_symmetric_effects_ignore_direction);
  RUN_TEST(test_names_and_enum_agree);
  RUN_TEST(test_upstream_indices_are_stable);
  return UNITY_END();
}
