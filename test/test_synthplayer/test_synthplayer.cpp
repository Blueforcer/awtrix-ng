#include <unity.h>

#include <algorithm>
#include <cmath>
#include <map>
#include <memory>
#include <set>
#include <string>
#include <vector>

#include "core/synth/Player.h"
#include "core/synth/SongParser.h"

using namespace awtrix;
using awtrix::synth::Player;
using awtrix::synth::Song;

void setUp() {}
void tearDown() {}

constexpr uint32_t kRate = 44100;
// At 120 bpm a beat is half a second and a 4/4 bar two.
constexpr std::size_t kBeat = kRate / 2;
constexpr std::size_t kBar = 4 * kBeat;

static std::shared_ptr<const Song> song(const std::string& text) {
  synth::ParseResult r = synth::parse(text);
  TEST_ASSERT_TRUE_MESSAGE(r.ok(), r.describe().c_str());
  return r.song;
}

static std::vector<float> render(Player& p, std::size_t frames, std::size_t block = 441) {
  std::vector<float> out(frames);
  for (std::size_t i = 0; i < frames; i += block) p.render(out.data() + i, std::min(block, frames - i));
  return out;
}

static std::size_t firstSound(const std::vector<float>& x, std::size_t from = 0) {
  for (std::size_t i = from; i < x.size(); ++i)
    if (x[i] != 0.0f) return i;
  return x.size();
}

static float energy(const std::vector<float>& x, std::size_t from, std::size_t count) {
  float sum = 0.0f;
  for (std::size_t i = from; i < from + count && i < x.size(); ++i) sum += x[i] * x[i];
  return sum;
}

static float peak(const std::vector<float>& x) {
  float m = 0.0f;
  for (float v : x) m = std::max(m, std::fabs(v));
  return m;
}

// Unity here is built without double support.
static void beatIs(double expected, double actual) {
  const std::string text = "beat " + std::to_string(actual) + ", expected " + std::to_string(expected);
  TEST_ASSERT_TRUE_MESSAGE(std::fabs(actual - expected) < 1e-6, text.c_str());
}

static std::size_t risingCrossings(const std::vector<float>& x, std::size_t from, std::size_t to) {
  std::size_t n = 0;
  for (std::size_t i = from + 1; i < to; ++i)
    if (x[i - 1] < 0.0f && x[i] >= 0.0f) ++n;
  return n;
}

// How many samples fall on the n values that occur most often.
static std::size_t onCommonest(const std::vector<float>& x, std::size_t from, std::size_t to,
                               std::size_t n) {
  std::map<float, std::size_t> counts;
  for (std::size_t i = from; i < to; ++i) ++counts[x[i]];
  std::vector<std::size_t> sizes;
  for (const auto& c : counts) sizes.push_back(c.second);
  std::sort(sizes.rbegin(), sizes.rend());
  std::size_t sum = 0;
  for (std::size_t i = 0; i < n && i < sizes.size(); ++i) sum += sizes[i];
  return sum;
}

static std::size_t distinctMagnitudes(const std::vector<float>& x) {
  std::set<float> seen;
  for (float v : x) seen.insert(std::fabs(v));
  return seen.size();
}

static float correlation(const std::vector<float>& x, std::size_t from, std::size_t count,
                         std::size_t lag) {
  float xy = 0.0f, xx = 0.0f, yy = 0.0f;
  for (std::size_t i = from; i < from + count; ++i) {
    xy += x[i] * x[i + lag];
    xx += x[i] * x[i];
    yy += x[i + lag] * x[i + lag];
  }
  return xy / std::sqrt(xx * yy);
}

static std::vector<float> held(const std::string& inst, const std::string& note,
                               std::size_t frames) {
  Player p(kRate, 1);
  p.play(song("inst a " + inst + " attack=0 sustain=100 gate=100\na: " + note + ":16"));
  return render(p, frames);
}

// A noise burst that is over in a few ms marks each note start.
static const std::string kTick = "inst n wave=noise attack=0 decay=2 sustain=0\n";

static void test_rendering_does_not_depend_on_the_block_size() {
  const auto s = song(
      "bpm 133\necho time=3 feedback=40 damp=2500\n"
      "inst lead wave=pulse duty=25 unison=10 vibrato=20/5/100 cutoff=3000 filterenv=2000/80 "
      "resonance=30 echo=50 drive=30\ninst bass wave=saw sub=40 cutoff=900 pitch=5/20\n"
      "inst bit wave=chiptri unison=8 vibrato=30/6/0 glide=200 levels=16\n"
      "inst hat wave=chipnoise note=g9 pitch=-24/30 decay=40 sustain=0 levels=16\n"
      "inst ring wave=chipmetal note=a7 decay=80 sustain=0\n" +
      kTick +
      "lead: e5:2 ^a c6:3 b5:1 [e4 g b]:4\nbass: (a2:1 a3)4\nn: %x.xo.x.xgx.x\n"
      "bit: c3:2 ^e5 ^g6 c3\nhat: %x.x.x.x.\nring: %..x...x.");
  Player a(kRate, 16), b(kRate, 16);
  a.play(s);
  b.play(s);
  const std::vector<float> small = render(a, 3 * kRate, 64);
  const std::vector<float> large = render(b, 3 * kRate, 1000);
  TEST_ASSERT_GREATER_THAN_FLOAT(0.05f, peak(small));
  TEST_ASSERT_TRUE(small == large);
}

static void test_a_note_starts_on_its_sample() {
  Player p(kRate, 4);
  p.play(song(kTick + "n: r:4 %x"));
  const std::vector<float> out = render(p, kBar);
  TEST_ASSERT_EQUAL_UINT(kBeat, firstSound(out));
}

static void test_the_pitch_is_right() {
  Player p(kRate, 4);
  p.play(song("inst a wave=sine attack=0\na: a4:16"));
  const std::vector<float> out = render(p, kRate);
  const std::size_t crossings = risingCrossings(out, kRate / 4, 3 * kRate / 4);
  TEST_ASSERT_UINT_WITHIN(1, 220, crossings);
}

static void test_a_note_releases_after_its_gate() {
  Player p(kRate, 4);
  p.play(song("loop off\ninst a wave=sine attack=0 release=50 gate=100\na: c4:4"));
  const std::vector<float> out = render(p, kBar);
  TEST_ASSERT_GREATER_THAN_FLOAT(0.01f, energy(out, kBeat - 441, 441));
  TEST_ASSERT_EQUAL_FLOAT(0.0f, energy(out, kBeat + kRate / 20 + 1, kRate / 10));
  TEST_ASSERT_TRUE(p.idle());
}

static void test_a_song_loops_back_to_its_loop_bar() {
  Player p(kRate, 4);
  p.play(song("loop 2\n" + kTick + "n: %x............... | r:4 %x"));
  const std::vector<float> out = render(p, 4 * kBar);
  const std::size_t onsets[] = {0, kBar + kBeat, 2 * kBar + kBeat, 3 * kBar + kBeat};
  for (std::size_t at : onsets) {
    TEST_ASSERT_EQUAL_UINT_MESSAGE(at, firstSound(out, at > 0 ? at - kBeat / 2 : 0),
                                   std::to_string(at).c_str());
  }
  TEST_ASSERT_EQUAL_FLOAT(0.0f, energy(out, kBar / 2, kBar / 2));
}

// At 157 bpm a bar lasts 67414.01 samples, so every pass ends just before a sample and the head
// arrives a little past the end. The loop bar's first beat still sounds, once, on its sample.
static void test_a_loop_that_ends_between_samples_keeps_its_first_beat() {
  Player p(kRate, 4);
  p.play(song("bpm 157\nloop 2\ninst a wave=sine attack=0 sustain=100 gate=100 release=1\n"
              "a: r:16 | c5:4"));
  const double bar = 4.0 * 60.0 / 157.0 * kRate;
  const std::vector<float> out = render(p, static_cast<std::size_t>(5.0 * bar));
  auto onset = [&](int pass) { return static_cast<std::size_t>(std::ceil(pass * bar)); };
  auto loudest = [&](std::size_t at) {
    return peak(std::vector<float>(out.begin() + at, out.begin() + at + kBeat / 2));
  };
  const std::size_t lag = firstSound(out) - onset(1);
  TEST_ASSERT_LESS_OR_EQUAL_UINT(1, lag);
  const float level = loudest(onset(1));
  for (int pass = 2; pass <= 4; ++pass) {
    const std::size_t at = onset(pass);
    const std::string where = "pass " + std::to_string(pass);
    TEST_ASSERT_EQUAL_UINT_MESSAGE(at + lag, firstSound(out, at - kBeat / 2), where.c_str());
    TEST_ASSERT_FLOAT_WITHIN_MESSAGE(0.01f * level, level, loudest(at), where.c_str());
  }
}

static void test_a_song_that_plays_once_ends() {
  Player p(kRate, 4);
  p.play(song("loop off\n" + kTick + "n: %x"));
  double beat = 0.0;
  TEST_ASSERT_TRUE(p.beat(beat));
  const std::vector<float> first = render(p, kBar + 441);
  TEST_ASSERT_FALSE(p.beat(beat));
  TEST_ASSERT_TRUE(p.idle());
  const std::vector<float> after = render(p, kBar);
  TEST_ASSERT_EQUAL_FLOAT(0.0f, peak(after));
  (void)first;
}

static void test_the_beat_follows_the_tempo() {
  Player p(kRate, 4);
  p.play(song("bpm 150\n" + kTick + "n: %:16x | %:16x"));
  render(p, kRate);
  double beat = 0.0;
  TEST_ASSERT_TRUE(p.beat(beat));
  beatIs(2.5, beat);
}

static void test_the_next_bar_hands_over_on_the_bar_line() {
  Player p(kRate, 4);
  p.play(song(kTick + "n: %x"));
  render(p, kBeat);
  p.play(song(kTick + "n: r:8 %x"), Player::Start::NextBar);
  const std::vector<float> out = render(p, 2 * kBar);
  // The first song would strike again at its loop point; the second takes over there instead.
  TEST_ASSERT_EQUAL_FLOAT(0.0f, energy(out, kBar - kBeat - 100, kBeat));
  TEST_ASSERT_EQUAL_UINT(kBar - kBeat + 2 * kBeat, firstSound(out, kBar - kBeat));
  double beat = 0.0;
  TEST_ASSERT_TRUE(p.beat(beat));
  // Two and a half seconds into a one-bar song that loops: one beat into its second pass.
  beatIs(1.0, beat);
}

static void test_the_same_song_again_keeps_playing() {
  Player p(kRate, 4);
  const auto s = song(kTick + "n: %:16x | %:16x");
  p.play(s);
  render(p, kRate);
  p.play(s);
  double beat = 0.0;
  TEST_ASSERT_TRUE(p.beat(beat));
  beatIs(2.0, beat);
  p.play(s, Player::Start::NextBar);
  TEST_ASSERT_TRUE(p.beat(beat));
  beatIs(2.0, beat);
}

static void test_stop_fades_out_within_10_ms() {
  Player p(kRate, 4);
  p.play(song("inst a wave=sine attack=0\na: c5:16"));
  render(p, kRate / 5);
  p.stop();
  const std::vector<float> fade = render(p, kRate / 100 + 1, kRate / 100 + 1);
  TEST_ASSERT_GREATER_THAN_FLOAT(0.0f, std::fabs(fade.front()) + std::fabs(fade[1]));
  TEST_ASSERT_EQUAL_FLOAT(0.0f, fade.back());
  TEST_ASSERT_TRUE(p.idle());
  double beat = 0.0;
  TEST_ASSERT_FALSE(p.beat(beat));
  TEST_ASSERT_EQUAL_FLOAT(0.0f, peak(render(p, kRate / 10)));
}

static void test_a_new_song_starts_while_the_old_one_fades() {
  Player p(kRate, 4);
  p.play(song("inst a wave=sine attack=0\na: c5:16"));
  render(p, kRate / 5);
  p.stop();
  p.play(song(kTick + "n: %x"));
  const std::vector<float> out = render(p, kBeat);
  TEST_ASSERT_EQUAL_UINT(0, firstSound(out));
  double beat = 0.0;
  TEST_ASSERT_TRUE(p.beat(beat));
}

static void test_voices_run_out_gracefully() {
  Player p(kRate, 4);
  p.play(song("inst a wave=saw attack=0\na: [c4 e g b d5 f a c6]:16"));
  const std::vector<float> out = render(p, kRate / 2);
  for (float v : out) TEST_ASSERT_TRUE(std::isfinite(v));
  TEST_ASSERT_GREATER_THAN_FLOAT(0.05f, peak(out));
}

static void test_the_output_stays_within_full_scale() {
  Player p(kRate, 24);
  p.play(song("volume 200\ninst a wave=saw drive=100 volume=200 attack=0\n"
              "a: [c2 e g b c3 e g b]:16\nb: @a [c4 e g b c5 e g b]:16\nc: @a [c6 e g b]:16"));
  const std::vector<float> out = render(p, kRate / 2);
  TEST_ASSERT_LESS_OR_EQUAL_FLOAT(1.0f, peak(out));
  TEST_ASSERT_GREATER_THAN_FLOAT(0.9f, peak(out));
}

static void test_the_echo_repeats_after_its_delay_and_dies_away() {
  Player p(kRate, 4);
  p.play(song("loop off\necho time=4 feedback=30 damp=16000\n"
              "inst n wave=noise attack=0 decay=2 sustain=0 echo=100\nn: %x"));
  const std::vector<float> out = render(p, 3 * kBar);
  const float direct = energy(out, 0, 441);
  const float first = energy(out, kBeat, 441);
  const float second = energy(out, 2 * kBeat, 441);
  TEST_ASSERT_GREATER_THAN_FLOAT(0.0f, direct);
  TEST_ASSERT_EQUAL_FLOAT(0.0f, energy(out, kBeat / 2, kBeat / 4));
  TEST_ASSERT_GREATER_THAN_FLOAT(0.1f * direct, first);
  TEST_ASSERT_LESS_THAN_FLOAT(first, second);
  TEST_ASSERT_GREATER_THAN_FLOAT(0.0f, second);
  TEST_ASSERT_TRUE(p.idle());
}

// A song without an echo sends nothing into the one the song before it left ringing.
static void test_a_song_without_an_echo_sends_into_none() {
  Player p(kRate, 4);
  p.play(song("echo time=4 feedback=30\ninst a wave=sine attack=0 decay=5 sustain=0\na: c4"));
  render(p, kBeat / 2);
  p.play(song("loop off\ninst n wave=noise attack=0 decay=2 sustain=0 echo=100\nn: %x"));
  const std::vector<float> out = render(p, 2 * kBeat);
  TEST_ASSERT_GREATER_THAN_FLOAT(0.0f, energy(out, 0, 441));
  TEST_ASSERT_EQUAL_FLOAT(0.0f, energy(out, kBeat - 441, 2 * 441));
}

static void test_a_low_pass_takes_out_the_highs() {
  auto roughness = [](const std::string& inst) {
    Player p(kRate, 4);
    p.play(song(inst + "\na: c5:16"));
    const std::vector<float> out = render(p, kRate / 2);
    float edges = 0.0f, body = 0.0f;
    for (std::size_t i = kRate / 10 + 1; i < out.size(); ++i) {
      edges += (out[i] - out[i - 1]) * (out[i] - out[i - 1]);
      body += out[i] * out[i];
    }
    return edges / body;
  };
  const float open = roughness("inst a wave=saw attack=0");
  const float closed = roughness("inst a wave=saw attack=0 cutoff=600");
  TEST_ASSERT_LESS_THAN_FLOAT(0.3f * open, closed);
}

static void test_an_effect_plays_once_even_if_its_song_loops() {
  Player p(kRate, 8, true);
  p.play(song(kTick + "n: %x"));
  const std::vector<float> out = render(p, 2 * kBar);
  TEST_ASSERT_EQUAL_FLOAT(0.0f, energy(out, kBar - 100, kBar));
  TEST_ASSERT_TRUE(p.idle());
}

static void test_a_slide_glides_without_a_new_attack() {
  auto around = [](const std::string& notes) {
    Player p(kRate, 4);
    p.play(song("inst a wave=sine attack=150 sustain=100 release=1 gate=100 glide=40\na: " +
                notes));
    const std::vector<float> out = render(p, kBar);
    return energy(out, kBeat + 100, 441) / energy(out, kBeat - 541, 441);
  };
  TEST_ASSERT_GREATER_THAN_FLOAT(0.7f, around("c4:4 ^c5:4"));
  TEST_ASSERT_LESS_THAN_FLOAT(0.3f, around("c4:4 c5:4"));
  Player p(kRate, 4);
  p.play(song("inst a wave=sine attack=0 sustain=100 gate=100 glide=40\na: c4:4 ^a4:8"));
  const std::vector<float> out = render(p, kBar);
  TEST_ASSERT_UINT_WITHIN(1, 220, risingCrossings(out, kBeat + kRate / 4, kBeat + 3 * kRate / 4));
}

static void test_extreme_settings_stay_finite() {
  Player p(kRate, 8);
  p.play(song("echo time=8 feedback=90 damp=200\ninst a wave=pulse duty=1 cutoff=20000 "
              "resonance=100 filterenv=-20000/1 pitch=48/1 vibrato=200/20/0 drive=100 echo=100 "
              "unison=100 sub=100 noise=100 filter=hp\n"
              "inst b wave=chiptri pitch=48/1 vibrato=200/20/0 unison=100 sub=100 levels=2\n"
              "inst c wave=chipmetal pitch=-48/1 noise=100 drive=100 levels=64\n"
              "a: c0:1 g9 ^c0 [c1 c9]:16\nb: c0:1 g9 ^c0 g9:16\nc: c0:1 g9 ^c0 c0:16"));
  const std::vector<float> out = render(p, 2 * kRate);
  for (float v : out) TEST_ASSERT_TRUE(std::isfinite(v));
  TEST_ASSERT_LESS_OR_EQUAL_FLOAT(1.0f, peak(out));
}

static void test_a_voice_at_volume_100_peaks_at_a_quarter_of_full_scale() {
  Player p(kRate, 1);
  p.play(song("inst a wave=sine attack=0 sustain=100 gate=100\na: a4:16"));
  const float level = peak(render(p, kRate / 2));
  TEST_ASSERT_FLOAT_WITHIN(0.005f, 0.25f, level);
}

static std::string noteName(int note) {
  static const char* const kNames[] = {"c", "c#", "d", "d#", "e", "f", "f#", "g", "g#", "a", "a#", "b"};
  return kNames[note % 12] + std::to_string(note / 12 - 1);
}

static void test_a_pulse_never_overshoots_its_level() {
  for (int duty = 5; duty <= 25; ++duty) {
    const float level = 0.1f * (2.0f - static_cast<float>(duty) / 50.0f);
    for (int note = 36; note <= 96; ++note) {
      const std::string name = noteName(note);
      Player p(kRate, 1);
      p.play(song("inst a wave=pulse duty=" + std::to_string(duty) +
                  " attack=0 sustain=100 gate=100 volume=40\na: " + name + ":16"));
      const std::string where = "duty " + std::to_string(duty) + ", " + name;
      TEST_ASSERT_LESS_OR_EQUAL_FLOAT_MESSAGE(1.01f * level, peak(render(p, kRate)), where.c_str());
    }
  }
}

static void test_a_chip_triangle_steps_through_16_levels() {
  const std::vector<float> stepped = held("wave=chiptri", "a1", kRate);
  const std::vector<float> smooth = held("wave=tri", "a1", kRate);
  const std::size_t from = kRate / 4, to = 3 * kRate / 4;
  TEST_ASSERT_GREATER_THAN_UINT((to - from) * 85 / 100, onCommonest(stepped, from, to, 16));
  TEST_ASSERT_LESS_THAN_UINT((to - from) / 10, onCommonest(smooth, from, to, 16));
  TEST_ASSERT_UINT_WITHIN(1, 27, risingCrossings(stepped, from, to));
}

static void test_a_chip_triangle_smooths_its_steps() {
  const std::vector<float> x = held("wave=chiptri", "a2", kRate / 2);
  float jump = 0.0f;
  for (std::size_t i = kRate / 8; i < x.size(); ++i) jump = std::max(jump, std::fabs(x[i] - x[i - 1]));
  // A step is 2/15 of the voice's quarter scale; smoothed, no sample takes more than 3/4 of it.
  TEST_ASSERT_LESS_THAN_FLOAT(0.8f * 0.25f * 2.0f / 15.0f, jump);
  TEST_ASSERT_GREATER_THAN_FLOAT(0.4f * 0.25f * 2.0f / 15.0f, jump);
}

static void test_a_chip_triangle_turns_plain_where_its_steps_would_alias() {
  TEST_ASSERT_TRUE(held("wave=chiptri", "c6", kRate / 4) == held("wave=tri", "c6", kRate / 4));
}

static void test_a_chip_triangle_never_overshoots_its_level() {
  for (int note = 24; note <= 108; ++note) {
    const std::string name = noteName(note);
    const float level = peak(held("wave=chiptri volume=40", name, kRate / 4));
    TEST_ASSERT_LESS_OR_EQUAL_FLOAT_MESSAGE(0.1001f, level, name.c_str());
  }
}

static void test_chip_noise_follows_the_note_in_16_steps() {
  const std::size_t half = kRate / 2;
  const std::vector<float> a4 = held("wave=chipnoise", "a4", half);
  TEST_ASSERT_TRUE(a4 == held("wave=chipnoise", "a#4", half));
  // a4 clocks the register at 1.79 MHz / 128, and about every other step flips the output.
  const std::size_t slow = risingCrossings(a4, 0, half);
  TEST_ASSERT_UINT_WITHIN(175, 1748, slow);
  const std::size_t fast = risingCrossings(held("wave=chipnoise", "a5", half), 0, half);
  TEST_ASSERT_UINT_WITHIN(slow / 5, 2 * slow, fast);
}

static void test_chip_metal_repeats_where_chip_noise_does_not() {
  // At a4 metal's 93 register steps take 293.3 samples, so three of them end near a whole one.
  const std::vector<float> metal = held("wave=chipmetal", "a4", kRate / 2);
  const std::vector<float> noise = held("wave=chipnoise", "a4", kRate / 2);
  TEST_ASSERT_GREATER_THAN_FLOAT(0.7f, correlation(metal, 1000, 8000, 880));
  TEST_ASSERT_LESS_THAN_FLOAT(0.3f, correlation(noise, 1000, 8000, 880));
}

static void test_every_chip_metal_hit_sounds_the_same() {
  const std::string hits = " attack=0 decay=30 sustain=0\na: %x............... | %x...............";
  Player metal(kRate, 1), noise(kRate, 1);
  metal.play(song("inst a wave=chipmetal" + hits));
  noise.play(song("inst a wave=chipnoise" + hits));
  const std::vector<float> m = render(metal, 2 * kBar);
  const std::vector<float> n = render(noise, 2 * kBar);
  const std::size_t hit = kRate / 20;
  TEST_ASSERT_GREATER_THAN_FLOAT(0.05f, peak(m));
  TEST_ASSERT_TRUE(std::equal(m.begin(), m.begin() + hit, m.begin() + kBar));
  TEST_ASSERT_FALSE(std::equal(n.begin(), n.begin() + hit, n.begin() + kBar));
}

static void test_levels_step_the_envelope() {
  const std::string note = " attack=0 decay=100 sustain=0\na: a4:16";
  Player stepped(kRate, 1), smooth(kRate, 1);
  stepped.play(song("inst a wave=chipnoise levels=4" + note));
  smooth.play(song("inst a wave=chipnoise" + note));
  // Silence and three levels above it.
  TEST_ASSERT_EQUAL_UINT(4, distinctMagnitudes(render(stepped, kRate / 2)));
  TEST_ASSERT_GREATER_THAN_UINT(100, distinctMagnitudes(render(smooth, kRate / 2)));
}

int main(int, char**) {
  UNITY_BEGIN();
  RUN_TEST(test_rendering_does_not_depend_on_the_block_size);
  RUN_TEST(test_a_note_starts_on_its_sample);
  RUN_TEST(test_the_pitch_is_right);
  RUN_TEST(test_a_note_releases_after_its_gate);
  RUN_TEST(test_a_song_loops_back_to_its_loop_bar);
  RUN_TEST(test_a_loop_that_ends_between_samples_keeps_its_first_beat);
  RUN_TEST(test_a_song_that_plays_once_ends);
  RUN_TEST(test_the_beat_follows_the_tempo);
  RUN_TEST(test_the_next_bar_hands_over_on_the_bar_line);
  RUN_TEST(test_the_same_song_again_keeps_playing);
  RUN_TEST(test_stop_fades_out_within_10_ms);
  RUN_TEST(test_a_new_song_starts_while_the_old_one_fades);
  RUN_TEST(test_voices_run_out_gracefully);
  RUN_TEST(test_the_output_stays_within_full_scale);
  RUN_TEST(test_the_echo_repeats_after_its_delay_and_dies_away);
  RUN_TEST(test_a_song_without_an_echo_sends_into_none);
  RUN_TEST(test_a_low_pass_takes_out_the_highs);
  RUN_TEST(test_an_effect_plays_once_even_if_its_song_loops);
  RUN_TEST(test_a_slide_glides_without_a_new_attack);
  RUN_TEST(test_extreme_settings_stay_finite);
  RUN_TEST(test_a_voice_at_volume_100_peaks_at_a_quarter_of_full_scale);
  RUN_TEST(test_a_pulse_never_overshoots_its_level);
  RUN_TEST(test_a_chip_triangle_steps_through_16_levels);
  RUN_TEST(test_a_chip_triangle_smooths_its_steps);
  RUN_TEST(test_a_chip_triangle_turns_plain_where_its_steps_would_alias);
  RUN_TEST(test_a_chip_triangle_never_overshoots_its_level);
  RUN_TEST(test_chip_noise_follows_the_note_in_16_steps);
  RUN_TEST(test_chip_metal_repeats_where_chip_noise_does_not);
  RUN_TEST(test_every_chip_metal_hit_sounds_the_same);
  RUN_TEST(test_levels_step_the_envelope);
  return UNITY_END();
}
