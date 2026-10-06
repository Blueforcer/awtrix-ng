#include <unity.h>

#include <string>

#include "core/synth/SongCache.h"
#include "core/synth/SongParser.h"

using namespace awtrix;
using awtrix::synth::Filter;
using awtrix::synth::Note;
using awtrix::synth::ParseResult;
using awtrix::synth::Song;
using awtrix::synth::SongVector;
using awtrix::synth::Wave;

void setUp() {}
void tearDown() {}

static const std::string kSine = "inst a wave=sine\n";

static const Song& ok(const std::string& text) {
  static ParseResult keep;
  keep = synth::parse(text);
  TEST_ASSERT_TRUE_MESSAGE(keep.ok(), (text + " -> " + keep.describe()).c_str());
  return *keep.song;
}

// The error names the reason and points at line and column, both from 1.
static ParseResult bad(const std::string& text, const std::string& reason, std::size_t line,
                       std::size_t column) {
  ParseResult p = synth::parse(text);
  TEST_ASSERT_FALSE_MESSAGE(p.ok(), ("expected a rejection: " + text).c_str());
  TEST_ASSERT_TRUE_MESSAGE(p.error.find(reason) != std::string::npos,
                           ("\"" + p.error + "\" lacks \"" + reason + "\"").c_str());
  TEST_ASSERT_EQUAL_UINT_MESSAGE(line, p.line, p.describe().c_str());
  TEST_ASSERT_EQUAL_UINT_MESSAGE(column, p.column, p.describe().c_str());
  return p;
}

static const SongVector<Note>& notes(const Song& song, std::size_t track = 0) {
  return song.tracks.at(track).notes;
}

static void test_a_minimal_song_takes_the_defaults() {
  const Song& s = ok(kSine + "a: c4");
  TEST_ASSERT_EQUAL_FLOAT(120.0f, s.bpm);
  TEST_ASSERT_EQUAL_UINT(4, s.beatsPerBar);
  TEST_ASSERT_TRUE(s.loops);
  TEST_ASSERT_EQUAL_UINT(0, s.loopTick);
  TEST_ASSERT_EQUAL_UINT(192, s.lengthTicks);
  TEST_ASSERT_EQUAL_UINT(1, s.tracks.size());
  const Note& n = notes(s)[0];
  TEST_ASSERT_EQUAL_UINT(0, n.tick);
  TEST_ASSERT_EQUAL_UINT(48, n.length);
  TEST_ASSERT_EQUAL_UINT(60, n.pitch);
  TEST_ASSERT_EQUAL_UINT(100, n.velocity);
  TEST_ASSERT_EQUAL_UINT(0, n.instrument);
  TEST_ASSERT_FALSE(n.slide);
}

static void test_octave_and_length_carry_on() {
  const Song& s = ok(kSine + "a: e5:2 a c6 b5");
  const auto& n = notes(s);
  TEST_ASSERT_EQUAL_UINT(4, n.size());
  const uint8_t pitches[] = {76, 81, 84, 83};
  for (std::size_t i = 0; i < 4; ++i) {
    TEST_ASSERT_EQUAL_UINT(pitches[i], n[i].pitch);
    TEST_ASSERT_EQUAL_UINT(24, n[i].length);
    TEST_ASSERT_EQUAL_UINT(24 * i, n[i].tick);
  }
}

static void test_sharps_and_flats() {
  const auto& n = notes(ok(kSine + "a: c#4 db4 bb3 b3 cb4 e#4"));
  const uint8_t pitches[] = {61, 61, 58, 59, 59, 65};
  for (std::size_t i = 0; i < 6; ++i) TEST_ASSERT_EQUAL_UINT(pitches[i], n[i].pitch);
}

static void test_rests_and_holds() {
  const Song& s = ok(kSine + "a: c4:4 r:4 d4:2 _:2 e");
  const auto& n = notes(s);
  TEST_ASSERT_EQUAL_UINT(3, n.size());
  TEST_ASSERT_EQUAL_UINT(96, n[1].tick);
  TEST_ASSERT_EQUAL_UINT(48, n[1].length);
  TEST_ASSERT_EQUAL_UINT(144, n[2].tick);
  TEST_ASSERT_EQUAL_UINT(24, n[2].length);
}

static void test_a_hold_needs_a_note_before_it() {
  bad(kSine + "a: _", "_ without a note", 2, 4);
  bad(kSine + "a: c4 r _", "_ without a note", 2, 9);
}

static void test_chords_start_together() {
  const Song& s = ok(kSine + "a: [c4 e g]:8 d");
  const auto& n = notes(s);
  TEST_ASSERT_EQUAL_UINT(4, n.size());
  const uint8_t pitches[] = {60, 64, 67};
  for (std::size_t i = 0; i < 3; ++i) {
    TEST_ASSERT_EQUAL_UINT(pitches[i], n[i].pitch);
    TEST_ASSERT_EQUAL_UINT(0, n[i].tick);
    TEST_ASSERT_EQUAL_UINT(96, n[i].length);
  }
  TEST_ASSERT_EQUAL_UINT(96, n[3].tick);
  TEST_ASSERT_EQUAL_UINT(62, n[3].pitch);
}

static void test_a_chord_is_held_as_a_whole() {
  const auto& n = notes(ok(kSine + "a: [c4 e]:4 _:4"));
  TEST_ASSERT_EQUAL_UINT(96, n[0].length);
  TEST_ASSERT_EQUAL_UINT(96, n[1].length);
}

static void test_chord_mistakes() {
  bad(kSine + "a: [c4 e:4]", "length goes after ]", 2, 9);
  bad(kSine + "a: [c4 e", "unclosed", 2, 4);
  bad(kSine + "a: []", "empty chord", 2, 4);
  bad(kSine + "a: [c d e f g a b c d]", "at most 8 chord notes", 2, 21);
  bad(kSine + "a: [c r]", "notes only", 2, 7);
}

static void test_a_repeat_plays_every_pass_the_same() {
  const auto& n = notes(ok(kSine + "a: (c:1 d5)2 e"));
  TEST_ASSERT_EQUAL_UINT(5, n.size());
  const uint8_t pitches[] = {60, 74, 60, 74, 76};
  for (std::size_t i = 0; i < 5; ++i) {
    TEST_ASSERT_EQUAL_UINT(pitches[i], n[i].pitch);
    TEST_ASSERT_EQUAL_UINT(12 * i, n[i].tick);
  }
}

static void test_repeats_nest() {
  const auto& n = notes(ok(kSine + "a: ((c:1)2 d:1)2"));
  const uint8_t pitches[] = {60, 60, 62, 60, 60, 62};
  TEST_ASSERT_EQUAL_UINT(6, n.size());
  for (std::size_t i = 0; i < 6; ++i) TEST_ASSERT_EQUAL_UINT(pitches[i], n[i].pitch);
}

static void test_repeat_mistakes() {
  bad(kSine + "a: (c d", "unclosed", 2, 4);
  bad(kSine + "a: (c d)", "repeat count", 2, 9);
  bad(kSine + "a: (c)0", "repeat must be 1..256", 2, 7);
  bad(kSine + "a: (c)257", "repeat must be 1..256", 2, 7);
  bad(kSine + "a: c)2", "closes no group", 2, 5);
  bad(kSine + "a: (((((c)2)2)2)2)2", "nest 4 deep at most", 2, 8);
}

static void test_a_runaway_repeat_is_refused() {
  bad(kSine + "a: ((((v1)256)256)256)256", "too many repeats", 2, 8);
}

static void test_bar_lines_check_the_count() {
  ok(kSine + "a: c4:16 | d:8 e:8 |");
  bad(kSine + "a: c4:15 |", "falls 15 sixteenths into bar 1", 2, 10);
  bad(kSine + "a: c4:16 | d:17 |", "falls 1 sixteenths into bar 3", 2, 17);
  bad(kSine + "a: c4:1/2 |", "falls 0.50 sixteenths into bar 1", 2, 11);
  ok("beats 3\n" + kSine + "a: c4:12 |");
}

static void test_lengths_and_triplets() {
  const auto& n = notes(ok(kSine + "a: c:4/3 d e f:1/12 g:256"));
  TEST_ASSERT_EQUAL_UINT(16, n[0].length);
  TEST_ASSERT_EQUAL_UINT(16, n[2].length);
  TEST_ASSERT_EQUAL_UINT(1, n[3].length);
  TEST_ASSERT_EQUAL_UINT(3072, n[4].length);
  bad(kSine + "a: c:0", "length must be 1..256", 2, 5);
  bad(kSine + "a: c:257", "length must be 1..256", 2, 5);
  bad(kSine + "a: c:1/5", "length too fine", 2, 5);
}

static void test_steps_are_sixteenths_with_velocity_marks() {
  const Song& t = ok("inst k wave=sine note=a1\nk: v50 %X.xog %:2x.");
  const auto& n = notes(t);
  TEST_ASSERT_EQUAL_UINT(5, n.size());
  const uint8_t velocities[] = {50, 40, 28, 15, 40};
  const uint32_t ticks[] = {0, 24, 36, 48, 60};
  for (std::size_t i = 0; i < 5; ++i) {
    TEST_ASSERT_EQUAL_UINT(33, n[i].pitch);
    TEST_ASSERT_EQUAL_UINT(velocities[i], n[i].velocity);
    TEST_ASSERT_EQUAL_UINT(ticks[i], n[i].tick);
  }
  TEST_ASSERT_EQUAL_UINT(12, n[0].length);
  TEST_ASSERT_EQUAL_UINT(24, n[4].length);
}

static void test_step_mistakes() {
  bad("inst k\nk: %x-x", "step must be X, x, o, g or .", 2, 6);
  bad("inst k\nk: %", "needs steps", 2, 4);
}

static void test_transpose_velocity_and_instrument_switches() {
  const Song& s = ok("inst a wave=sine\ninst b wave=saw note=c3\na: t+12 c4 t-5 c4 t0 v40 c4 @b %x");
  const auto& n = notes(s);
  TEST_ASSERT_EQUAL_UINT(72, n[0].pitch);
  TEST_ASSERT_EQUAL_UINT(55, n[1].pitch);
  TEST_ASSERT_EQUAL_UINT(60, n[2].pitch);
  TEST_ASSERT_EQUAL_UINT(40, n[2].velocity);
  TEST_ASSERT_EQUAL_UINT(0, n[2].instrument);
  TEST_ASSERT_EQUAL_UINT(1, n[3].instrument);
  TEST_ASSERT_EQUAL_UINT(48, n[3].pitch);
  bad(kSine + "a: v0 c", "v1..v100", 2, 4);
  bad(kSine + "a: t49 c", "t-48..t+48", 2, 4);
  bad(kSine + "a: @nope c", "no instrument called 'nope'", 2, 4);
}

static void test_slides_are_marked() {
  const auto& n = notes(ok(kSine + "a: c4 ^e"));
  TEST_ASSERT_FALSE(n[0].slide);
  TEST_ASSERT_TRUE(n[1].slide);
  bad(kSine + "a: ^r", "^ needs a note", 2, 4);
}

static void test_note_mistakes() {
  bad(kSine + "a: c4:2d4", "expected a space after 'c4:2'", 2, 8);
  bad(kSine + "a: c44", "octave must be 0..9", 2, 4);
  bad(kSine + "a: a9", "out of range", 2, 4);
  ok(kSine + "a: g9");
  bad(kSine + "a: c4 x", "unexpected 'x'", 2, 7);
}

static void test_tracks_play_their_namesake_or_a_named_instrument() {
  const Song& s = ok("inst lead wave=pulse\ninst pad wave=saw\nlead: c4\nbass: @pad c2");
  TEST_ASSERT_EQUAL_UINT(0, notes(s, 0)[0].instrument);
  TEST_ASSERT_EQUAL_UINT(1, notes(s, 1)[0].instrument);
  bad("inst lead\nbass: c2", "track 'bass' has no instrument", 2, 7);
}

static void test_track_lines_continue_and_may_come_first() {
  const Song& s = ok("a: c4:16 |\nbpm 90\na: d4:16 |\ninst a wave=tri");
  TEST_ASSERT_EQUAL_UINT(2, notes(s).size());
  TEST_ASSERT_EQUAL_UINT(192, notes(s)[1].tick);
  TEST_ASSERT_EQUAL_UINT(384, s.lengthTicks);
  TEST_ASSERT_EQUAL_FLOAT(90.0f, s.bpm);
}

static void test_the_song_ends_on_a_whole_bar() {
  const Song& s = ok(kSine + "a: c4:17\nb: @a c4:2");
  TEST_ASSERT_EQUAL_UINT(2, s.tracks.size());
  TEST_ASSERT_EQUAL_UINT(384, s.lengthTicks);
  TEST_ASSERT_EQUAL_UINT(204, s.endTick);
  TEST_ASSERT_EQUAL_UINT(384, s.endOf(false));
  TEST_ASSERT_EQUAL_UINT(204, s.endOf(true));
  TEST_ASSERT_EQUAL_UINT(204, ok("loop off\n" + kSine + "a: c4:17").endOf(false));
}

static void test_loop_points() {
  const Song& s = ok("loop 2\n" + kSine + "a: c4:16 | d4:16");
  TEST_ASSERT_TRUE(s.loops);
  TEST_ASSERT_EQUAL_UINT(192, s.loopTick);
  TEST_ASSERT_FALSE(ok("loop off\n" + kSine + "a: c4").loops);
  bad("loop 3\n" + kSine + "a: c4:16 | d4:16", "loop 3 is past the last bar (2)", 1,
      6);
  bad("loop 0\n" + kSine + "a: c4", "between 1 and 1024", 1, 6);
}

static void test_song_settings() {
  const Song& s = ok("bpm 151.5\nbeats 3\nvolume 80\necho time=3 feedback=30 damp=3000\n" + kSine +
                     "a: c4");
  TEST_ASSERT_EQUAL_FLOAT(151.5f, s.bpm);
  TEST_ASSERT_EQUAL_UINT(3, s.beatsPerBar);
  TEST_ASSERT_EQUAL_FLOAT(0.8f, s.volume);
  TEST_ASSERT_EQUAL_FLOAT(3.0f, s.echoSixteenths);
  TEST_ASSERT_EQUAL_FLOAT(0.3f, s.echoFeedback);
  TEST_ASSERT_EQUAL_FLOAT(3000.0f, s.echoDampHz);
  bad("bpm 10\n" + kSine + "a: c", "bpm must be between 20 and 300", 1, 5);
  bad("bpm 120\nbpm 130\n" + kSine + "a: c", "'bpm' is set twice", 2, 1);
  bad("bpm\n" + kSine + "a: c", "'bpm' takes one value", 1, 4);
  bad("bpm 1 2\n" + kSine + "a: c", "'bpm' takes one value", 1, 7);
  bad("beats 2.5\n" + kSine + "a: c", "whole number", 1, 7);
  bad("tempo 120\n" + kSine + "a: c", "unknown statement 'tempo'", 1, 1);
  bad(kSine + "a : c4 d4", "unknown statement 'a'", 2, 1);
  bad("echo feedback=30\n" + kSine + "a: c", "echo needs time", 1, 1);
  bad("echo time=3 wet=2\n" + kSine + "a: c", "unknown echo setting 'wet'", 1, 13);
  bad("bpm 20\necho time=64\n" + kSine + "a: c", "over 2 s", 2, 11);
}

static void test_instrument_settings() {
  const Song& s = ok(
      "inst x wave=saw duty=25 unison=12 sub=35 noise=30/4 attack=5 decay=250 sustain=75 "
      "release=90 pitch=24/28 vibrato=18/5.6/160 glide=40 filter=bp cutoff=1200 resonance=50 "
      "filterenv=4000/50 drive=60 volume=120 echo=40 gate=80 note=f#2\nx: c4");
  const synth::Patch& p = s.instruments[0].patch;
  TEST_ASSERT_EQUAL_STRING("x", s.instruments[0].name.c_str());
  TEST_ASSERT_TRUE(p.wave == Wave::Saw);
  TEST_ASSERT_EQUAL_FLOAT(0.25f, p.duty);
  TEST_ASSERT_EQUAL_FLOAT(12.0f, p.unison);
  TEST_ASSERT_EQUAL_FLOAT(0.35f, p.sub);
  TEST_ASSERT_EQUAL_FLOAT(0.3f, p.noise);
  TEST_ASSERT_EQUAL_FLOAT(4.0f, p.noiseDecayMs);
  TEST_ASSERT_EQUAL_FLOAT(5.0f, p.attackMs);
  TEST_ASSERT_EQUAL_FLOAT(250.0f, p.decayMs);
  TEST_ASSERT_EQUAL_FLOAT(0.75f, p.sustain);
  TEST_ASSERT_EQUAL_FLOAT(90.0f, p.releaseMs);
  TEST_ASSERT_EQUAL_FLOAT(24.0f, p.pitchSemis);
  TEST_ASSERT_EQUAL_FLOAT(28.0f, p.pitchMs);
  TEST_ASSERT_EQUAL_FLOAT(18.0f, p.vibratoCents);
  TEST_ASSERT_EQUAL_FLOAT(5.6f, p.vibratoHz);
  TEST_ASSERT_EQUAL_FLOAT(160.0f, p.vibratoDelayMs);
  TEST_ASSERT_EQUAL_FLOAT(40.0f, p.glideMs);
  TEST_ASSERT_TRUE(p.filter == Filter::BandPass);
  TEST_ASSERT_EQUAL_FLOAT(1200.0f, p.cutoffHz);
  TEST_ASSERT_EQUAL_FLOAT(0.5f, p.resonance);
  TEST_ASSERT_EQUAL_FLOAT(4000.0f, p.filterEnvHz);
  TEST_ASSERT_EQUAL_FLOAT(50.0f, p.filterEnvMs);
  TEST_ASSERT_EQUAL_FLOAT(0.6f, p.drive);
  TEST_ASSERT_EQUAL_FLOAT(1.2f, p.volume);
  TEST_ASSERT_EQUAL_FLOAT(0.4f, p.echo);
  TEST_ASSERT_EQUAL_FLOAT(0.8f, p.gate);
  TEST_ASSERT_EQUAL_UINT(42, p.stepPitch);
}

static void test_a_cutoff_alone_means_a_low_pass() {
  TEST_ASSERT_TRUE(ok("inst x cutoff=800\nx: c").instruments[0].patch.filter == Filter::LowPass);
  TEST_ASSERT_TRUE(ok("inst x\nx: c").instruments[0].patch.filter == Filter::None);
}

static void test_instrument_mistakes() {
  bad("inst\nx: c", "inst needs a name", 1, 5);
  bad("inst x wave=square\nx: c", "wave is pulse, saw, tri, sine or noise", 1, 13);
  bad("inst x duty=0\nx: c", "duty must be between 1 and 99", 1, 13);
  bad("inst x vibrato=1/2/3/4\nx: c", "at most 3 values", 1, 22);
  bad("inst x duty=2/3\nx: c", "at most 1 value", 1, 15);
  bad("inst x colour=red\nx: c", "unknown instrument setting 'colour'", 1, 8);
  bad("inst x duty\nx: c", "key=value", 1, 8);
  bad("inst x duty=abc\nx: c", "expected a number", 1, 13);
  bad("inst x note=h4\nx: c", "expected a note", 1, 13);
  bad("inst x\ninst x\nx: c", "defined twice", 2, 6);
  bad("inst waytoolongname123\nx: c", "invalid instrument name", 1, 6);
}

static void test_comments_and_separators() {
  const Song& s = ok("# a song\nbpm 100 # slow;\ninst a wave=sine;a: c#4 # the note; d\n;;");
  TEST_ASSERT_EQUAL_FLOAT(100.0f, s.bpm);
  TEST_ASSERT_EQUAL_UINT(1, notes(s).size());
  TEST_ASSERT_EQUAL_UINT(61, notes(s)[0].pitch);
}

static void test_a_song_must_play_a_note() {
  bad(kSine + "a: r:16", "no notes", 2, 8);
  bad("", "no notes", 1, 1);
}

static void test_limits() {
  bad(std::string(synth::kMaxSongBytes + 1, ' '), "at most 16384 bytes", 1, 16385);
  std::string many = kSine;
  for (int i = 0; i < 17; ++i) many += "t" + std::to_string(i) + ": @a c\n";
  bad(many, "at most 16 tracks", 18, 1);
  std::string insts;
  for (int i = 0; i < 33; ++i) insts += "inst i" + std::to_string(i) + "\n";
  bad(insts + "i0: c", "at most 32 instruments", 33, 6);
  bad(kSine + "a: ((c:1)128)65", "at most 8192 notes", 2, 6);
  bad(kSine + "a: (c:256)65", "at most 1024 bars", 2, 5);
  bad(kSine + "waytoolongtrackname: @a c", "invalid track name", 2, 1);
}

static void test_describe_names_line_and_column() {
  const ParseResult p = synth::parse("bpm 120\ninst a\na: c4 x4");
  TEST_ASSERT_FALSE(p.ok());
  TEST_ASSERT_FALSE(p.error.empty());
  TEST_ASSERT_EQUAL_UINT(3, p.line);
  TEST_ASSERT_EQUAL_UINT(7, p.column);
  const std::string description = p.describe();
  TEST_ASSERT_TRUE(description.find(p.error) != std::string::npos);
  TEST_ASSERT_TRUE(description.find(std::to_string(p.line)) != std::string::npos);
  TEST_ASSERT_TRUE(description.find(std::to_string(p.column)) != std::string::npos);
  TEST_ASSERT_TRUE(synth::parse(kSine + "a: c").describe().empty());
}

static void test_the_cache_hands_out_one_song_per_text() {
  synth::SongCache cache;
  const ParseResult a = cache.get(kSine + "a: c");
  const ParseResult b = cache.get(kSine + "a: c");
  const ParseResult c = cache.get(kSine + "a: d");
  TEST_ASSERT_TRUE(a.ok() && b.ok() && c.ok());
  TEST_ASSERT_TRUE(a.song == b.song);
  TEST_ASSERT_TRUE(a.song != c.song);
  TEST_ASSERT_EQUAL_UINT(2, cache.size());
}

static void test_the_cache_keeps_no_failure() {
  synth::SongCache cache;
  const ParseResult valid = cache.get(kSine + "a: c");
  TEST_ASSERT_TRUE(valid.ok());
  const std::size_t retained = cache.bytes();
  const ParseResult p = cache.get(kSine + "a: x");
  TEST_ASSERT_FALSE(p.ok());
  TEST_ASSERT_FALSE(p.error.empty());
  TEST_ASSERT_EQUAL_UINT(2, p.line);
  TEST_ASSERT_EQUAL_UINT(4, p.column);
  TEST_ASSERT_EQUAL_UINT(retained, cache.bytes());
  TEST_ASSERT_TRUE(cache.get(kSine + "a: c").song == valid.song);
}

static void test_the_cache_drops_the_song_used_longest_ago() {
  synth::SongCache probe;
  TEST_ASSERT_TRUE(probe.get(kSine + "a: c").ok());
  const std::size_t one = probe.bytes();
  TEST_ASSERT_GREATER_THAN_UINT(0, one);
  const std::size_t budget = 2 * one + one / 2;
  synth::SongCache cache(budget);
  const auto first = cache.get(kSine + "a: c").song;
  const auto second = cache.get(kSine + "a: d").song;
  TEST_ASSERT_NOT_NULL(first.get());
  TEST_ASSERT_NOT_NULL(second.get());
  TEST_ASSERT_TRUE(cache.get(kSine + "a: c").song == first);
  TEST_ASSERT_TRUE(cache.get(kSine + "a: e").ok());
  TEST_ASSERT_TRUE(cache.get(kSine + "a: c").song == first);
  TEST_ASSERT_TRUE(cache.get(kSine + "a: d").song != second);
  TEST_ASSERT_LESS_OR_EQUAL_UINT(budget, cache.bytes());
}

static void test_a_song_above_the_budget_plays_without_being_kept() {
  synth::SongCache probe;
  TEST_ASSERT_TRUE(probe.get(kSine + "a: c").ok());
  TEST_ASSERT_GREATER_THAN_UINT(0, probe.bytes());
  const std::size_t budget = probe.bytes() - 1;
  synth::SongCache cache(budget);
  const ParseResult p = cache.get(kSine + "a: c");
  TEST_ASSERT_TRUE(p.ok());
  const ParseResult again = cache.get(kSine + "a: c");
  TEST_ASSERT_TRUE(again.ok());
  TEST_ASSERT_TRUE(p.song != again.song);
  TEST_ASSERT_LESS_OR_EQUAL_UINT(budget, cache.bytes());
}

int main(int, char**) {
  UNITY_BEGIN();
  RUN_TEST(test_a_minimal_song_takes_the_defaults);
  RUN_TEST(test_octave_and_length_carry_on);
  RUN_TEST(test_sharps_and_flats);
  RUN_TEST(test_rests_and_holds);
  RUN_TEST(test_a_hold_needs_a_note_before_it);
  RUN_TEST(test_chords_start_together);
  RUN_TEST(test_a_chord_is_held_as_a_whole);
  RUN_TEST(test_chord_mistakes);
  RUN_TEST(test_a_repeat_plays_every_pass_the_same);
  RUN_TEST(test_repeats_nest);
  RUN_TEST(test_repeat_mistakes);
  RUN_TEST(test_a_runaway_repeat_is_refused);
  RUN_TEST(test_bar_lines_check_the_count);
  RUN_TEST(test_lengths_and_triplets);
  RUN_TEST(test_steps_are_sixteenths_with_velocity_marks);
  RUN_TEST(test_step_mistakes);
  RUN_TEST(test_transpose_velocity_and_instrument_switches);
  RUN_TEST(test_slides_are_marked);
  RUN_TEST(test_note_mistakes);
  RUN_TEST(test_tracks_play_their_namesake_or_a_named_instrument);
  RUN_TEST(test_track_lines_continue_and_may_come_first);
  RUN_TEST(test_the_song_ends_on_a_whole_bar);
  RUN_TEST(test_loop_points);
  RUN_TEST(test_song_settings);
  RUN_TEST(test_instrument_settings);
  RUN_TEST(test_a_cutoff_alone_means_a_low_pass);
  RUN_TEST(test_instrument_mistakes);
  RUN_TEST(test_comments_and_separators);
  RUN_TEST(test_a_song_must_play_a_note);
  RUN_TEST(test_limits);
  RUN_TEST(test_describe_names_line_and_column);
  RUN_TEST(test_the_cache_hands_out_one_song_per_text);
  RUN_TEST(test_the_cache_keeps_no_failure);
  RUN_TEST(test_the_cache_drops_the_song_used_longest_ago);
  RUN_TEST(test_a_song_above_the_budget_plays_without_being_kept);
  return UNITY_END();
}
