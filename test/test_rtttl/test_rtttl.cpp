#include <unity.h>

#include <string>

#include "core/sound/Rtttl.h"

using namespace awtrix;
using awtrix::rtttl::Note;
using awtrix::rtttl::Parse;

void setUp() {}
void tearDown() {}

static Parse ok(const std::string& s) {
  Parse p = rtttl::parse(s);
  TEST_ASSERT_TRUE_MESSAGE(p.ok, (s + " -> " + p.error).c_str());
  return p;
}

static void bad(const std::string& s) {
  Parse p = rtttl::parse(s);
  TEST_ASSERT_FALSE_MESSAGE(p.ok, ("expected a rejection: " + s).c_str());
  TEST_ASSERT_FALSE_MESSAGE(p.error.empty(), ("rejection without a reason: " + s).c_str());
}

static void test_three_parts_required() {
  bad("");
  bad("beep");
  bad("beep:d=4,o=5,b=120");
  bad(":d=4,o=5,b=120:c,e,g");
  bad("   :d=4,o=5,b=120:c");
  bad("d=4,o=5,b=120:c,e,g");
  bad("beep:d=4,o=5,b=120:");
  bad("beep:d=4,o=5,b=120:   ");
}

static void test_title_length_capped() {
  ok(std::string("a", 1) + ":d=4,o=5,b=120:c");
  ok(std::string(rtttl::kMaxTitle, 'a') + ":d=4,o=5,b=120:c");
  bad(std::string(rtttl::kMaxTitle + 1, 'a') + ":d=4,o=5,b=120:c");
}

static void test_title_is_trimmed_and_kept() {
  Parse p = ok("  doorbell :d=4,o=5,b=100:e,c");
  TEST_ASSERT_EQUAL_STRING("doorbell", p.title.c_str());
}

static void test_trailing_newline_parses() {
  Parse p = ok("beep:d=4,o=5,b=120:c,e,g\n");
  TEST_ASSERT_EQUAL_UINT(3, p.notes.size());
  ok("beep:d=4,o=5,b=120:c,e,g\r\n");
}

static void test_length_capped() {
  const std::string head = "x:d=4,o=5,b=120:";
  std::string notes = "c";
  while (head.size() + notes.size() < rtttl::kMaxLength) notes += ",c";
  bad(head + notes + ",c,c,c,c");
}

static void test_a_long_melody_still_fits() {
  std::string s = "x:d=32,o=5,b=300:c";
  while (s.size() + 2 <= rtttl::kMaxLength) s += ",c";
  Parse p = ok(s);
  TEST_ASSERT_TRUE(p.notes.size() > 200);
}

static void test_defaults_any_order_and_optional() {
  Parse a = ok("x:d=8,o=6,b=200:c");
  Parse b = ok("x:b=200,o=6,d=8:c");
  TEST_ASSERT_EQUAL_UINT16(a.timeUnit, b.timeUnit);
  TEST_ASSERT_EQUAL_UINT16(a.notes[0].frequency, b.notes[0].frequency);
  TEST_ASSERT_EQUAL_UINT16(a.notes[0].duration, b.notes[0].duration);

  ok("x::c");
  ok("x:b=120:c");
  ok("x: d = 8 , o = 5 , b = 120 :c");
}

static void test_defaults_fall_back_to_the_factory_values() {
  Parse p = ok("x::c");
  TEST_ASSERT_EQUAL_UINT16(16, p.notes[0].duration);
  TEST_ASSERT_EQUAL_UINT16(1047, p.notes[0].frequency);
  TEST_ASSERT_EQUAL_UINT16(119, p.timeUnit);
}

static void test_bad_default_values_rejected() {
  bad("x:d=99,o=5,b=120:c");
  bad("x:d=3,o=5,b=120:c");
  bad("x:d=0,o=5,b=120:c");
  bad("x:d=4,o=1,b=120:c");
  bad("x:d=4,o=8,b=120:c");
  bad("x:d=4,o=5,b=9:c");
  bad("x:d=4,o=5,b=301:c");
  bad("x:d=4,o=5,b=:c");
  bad("x:d=,o=5,b=120:c");
  bad("x:z=4:c");
  bad("x:d4,o=5,b=120:c");
}

static void test_duplicate_assignment_rejected() {
  bad("x:d=4,d=8,b=120:c");
  bad("x:o=5,o=6:c");
}

static void test_note_letters_and_rests() {
  Parse p = ok("x:d=4,o=4,b=120:c,d,e,f,g,a,b,p");
  const uint16_t want[] = {262, 294, 330, 349, 392, 440, 494, 0};
  TEST_ASSERT_EQUAL_UINT(8, p.notes.size());
  for (size_t i = 0; i < 8; ++i) TEST_ASSERT_EQUAL_UINT16(want[i], p.notes[i].frequency);
}

static void test_sharps() {
  Parse p = ok("x:d=4,o=4,b=120:c#,d#,f#,g#,a#");
  const uint16_t want[] = {277, 311, 370, 415, 466};
  for (size_t i = 0; i < 5; ++i) TEST_ASSERT_EQUAL_UINT16(want[i], p.notes[i].frequency);
}

static void test_octaves_span_four_to_seven() {
  Parse p = ok("x:d=4,o=5,b=120:c4,c5,c6,c7");
  const uint16_t want[] = {262, 523, 1047, 2093};
  for (size_t i = 0; i < 4; ++i) TEST_ASSERT_EQUAL_UINT16(want[i], p.notes[i].frequency);
}

static void test_out_of_range_octaves_rejected() {
  bad("x:d=4,o=5,b=120:c8");
  bad("x:d=4,o=5,b=120:c3");
  bad("x:d=4,o=5,b=120:c0");
  bad("x:d=4,o=5,b=120:c9");
}

static void test_enharmonic_sharps_rejected() {
  bad("x:d=4,o=5,b=120:b#");
  bad("x:d=4,o=5,b=120:e#");
  bad("x:d=4,o=7,b=120:b#7");
}

static void test_rest_takes_no_sharp() {
  bad("x:d=4,o=5,b=120:p#");
}

static void test_unknown_note_letter_rejected() {
  bad("x:d=4,o=5,b=120:h");
  bad("x:d=4,o=5,b=120:zzz");
  bad("x:d=4,o=5,b=120:c,,e");
  bad("x:d=4,o=5,b=120:c,e,");
  bad("x:d=4,o=5,b=120:4");
  bad("x:d=4,o=5,b=120:c5x");
}

static void test_bad_note_duration_rejected() {
  bad("x:d=4,o=5,b=120:3c");
  bad("x:d=4,o=5,b=120:64c");
  bad("x:d=4,o=5,b=120:0c");
}

static void test_durations_match_the_old_factory() {
  Parse p = ok("x:d=4,o=5,b=120:1c,2c,4c,8c,16c,32c,c");
  const uint16_t want[] = {64, 32, 16, 8, 4, 2, 16};
  for (size_t i = 0; i < 7; ++i) TEST_ASSERT_EQUAL_UINT16(want[i], p.notes[i].duration);
}

static void test_dotted_notes_add_half() {
  Parse p = ok("x:d=4,o=5,b=120:4c.,4c");
  TEST_ASSERT_EQUAL_UINT16(24, p.notes[0].duration);
  TEST_ASSERT_EQUAL_UINT16(16, p.notes[1].duration);
}

static void test_dot_accepted_on_either_side_of_the_octave() {
  Parse a = ok("x:d=4,o=5,b=120:4c.6");
  Parse b = ok("x:d=4,o=5,b=120:4c6.");
  TEST_ASSERT_EQUAL_UINT16(a.notes[0].duration, b.notes[0].duration);
  TEST_ASSERT_EQUAL_UINT16(a.notes[0].frequency, b.notes[0].frequency);
  TEST_ASSERT_EQUAL_UINT16(1047, a.notes[0].frequency);
  bad("x:d=4,o=5,b=120:4c..");
  bad("x:d=4,o=5,b=120:4c.6.");
}

static void test_time_unit_matches_the_old_formula() {
  TEST_ASSERT_EQUAL_UINT16(75, ok("x:d=4,o=5,b=100:c").timeUnit);
  TEST_ASSERT_EQUAL_UINT16(62, ok("x:d=4,o=5,b=120:c").timeUnit);
  TEST_ASSERT_EQUAL_UINT16(25, ok("x:d=4,o=5,b=300:c").timeUnit);
  TEST_ASSERT_EQUAL_UINT16(750, ok("x:d=4,o=5,b=10:c").timeUnit);
}

static void test_duration_ms_totals_the_notes() {
  Parse p = ok("doorbell:d=4,o=5,b=100:e,c");
  TEST_ASSERT_EQUAL_UINT32(1200u, p.durationMs());
}

static void test_duration_ms_matches_a_beat() {
  TEST_ASSERT_EQUAL_UINT32(1984u, ok("w:d=4,o=5,b=120:1c").durationMs());
  TEST_ASSERT_EQUAL_UINT32(496u, ok("q:d=4,o=5,b=120:4c").durationMs());
  TEST_ASSERT_EQUAL_UINT32(744u, ok("d:d=4,o=5,b=120:4c.").durationMs());
}

static void test_doc_example_beep() {
  Parse p = ok("beep:d=4,o=5,b=120:c,e,g");
  TEST_ASSERT_EQUAL_UINT(3, p.notes.size());
  TEST_ASSERT_EQUAL_UINT16(523, p.notes[0].frequency);
  TEST_ASSERT_EQUAL_UINT16(659, p.notes[1].frequency);
  TEST_ASSERT_EQUAL_UINT16(784, p.notes[2].frequency);
  for (const Note& n : p.notes) TEST_ASSERT_EQUAL_UINT16(16, n.duration);
}

static void test_doc_example_doorbell() {
  Parse p = ok("bell:d=4,o=5,b=100:e,c");
  TEST_ASSERT_EQUAL_UINT16(659, p.notes[0].frequency);
  TEST_ASSERT_EQUAL_UINT16(523, p.notes[1].frequency);
}

static void test_doc_example_loose() {
  Parse p = ok("loose:d=8,o=5,b=120:16c,16b,16a,4g");
  TEST_ASSERT_EQUAL_UINT(4, p.notes.size());
  TEST_ASSERT_EQUAL_UINT16(523, p.notes[0].frequency);
  TEST_ASSERT_EQUAL_UINT16(988, p.notes[1].frequency);
  TEST_ASSERT_EQUAL_UINT16(880, p.notes[2].frequency);
  TEST_ASSERT_EQUAL_UINT16(784, p.notes[3].frequency);
  TEST_ASSERT_EQUAL_UINT16(4, p.notes[0].duration);
  TEST_ASSERT_EQUAL_UINT16(16, p.notes[3].duration);
}

static void test_doc_example_jackpot() {
  Parse p = ok("jackpot:d=8,o=5,b=120:16c,16e,16g,c6,16p,16c6,16e6,4g6");
  TEST_ASSERT_EQUAL_UINT(8, p.notes.size());
  TEST_ASSERT_EQUAL_UINT16(0, p.notes[4].frequency);
  TEST_ASSERT_EQUAL_UINT16(1568, p.notes[7].frequency);
  TEST_ASSERT_EQUAL_UINT32(1488u, p.durationMs());
}

static void test_builtin_r2d2_still_parses() {
  Parse p = ok("r2d2:d=4,o=5,b=240:16c6,16g6,16e6,16a6,16g6,16e7");
  TEST_ASSERT_EQUAL_UINT(6, p.notes.size());
  TEST_ASSERT_EQUAL_UINT16(2637, p.notes[5].frequency);
}

static void test_error_index_points_at_the_offending_byte() {
  Parse p = rtttl::parse("beep:d=4,o=5,b=120:c,e,h");
  TEST_ASSERT_FALSE(p.ok);
  TEST_ASSERT_EQUAL_UINT(23, p.index);
}

static void test_valid_name() {
  TEST_ASSERT_TRUE(rtttl::validName("doorbell"));
  TEST_ASSERT_TRUE(rtttl::validName("a"));
  TEST_ASSERT_TRUE(rtttl::validName("A_b-2"));
  TEST_ASSERT_TRUE(rtttl::validName(std::string(24, 'x')));
  TEST_ASSERT_FALSE(rtttl::validName(""));
  TEST_ASSERT_FALSE(rtttl::validName(std::string(25, 'x')));
  TEST_ASSERT_FALSE(rtttl::validName("has space"));
  TEST_ASSERT_FALSE(rtttl::validName("dot.txt"));
  TEST_ASSERT_FALSE(rtttl::validName("../etc"));
  TEST_ASSERT_FALSE(rtttl::validName("colon:name"));
}

static void test_retitle_rewrites_the_title() {
  std::string out;
  TEST_ASSERT_TRUE(rtttl::retitle("bell:d=4,o=5,b=100:e,c", "doorbell", out));
  TEST_ASSERT_EQUAL_STRING("doorbell:d=4,o=5,b=100:e,c", out.c_str());
}

static void test_retitle_prepends_to_a_two_part_string() {
  std::string out;
  TEST_ASSERT_TRUE(rtttl::retitle("d=4,o=5,b=100:e,c", "doorbell", out));
  TEST_ASSERT_EQUAL_STRING("doorbell:d=4,o=5,b=100:e,c", out.c_str());
}

static void test_retitle_needs_a_separator() {
  std::string out;
  TEST_ASSERT_FALSE(rtttl::retitle("no separators here", "x", out));
}

int main(int, char**) {
  UNITY_BEGIN();
  RUN_TEST(test_three_parts_required);
  RUN_TEST(test_title_length_capped);
  RUN_TEST(test_title_is_trimmed_and_kept);
  RUN_TEST(test_trailing_newline_parses);
  RUN_TEST(test_length_capped);
  RUN_TEST(test_a_long_melody_still_fits);
  RUN_TEST(test_defaults_any_order_and_optional);
  RUN_TEST(test_defaults_fall_back_to_the_factory_values);
  RUN_TEST(test_bad_default_values_rejected);
  RUN_TEST(test_duplicate_assignment_rejected);
  RUN_TEST(test_note_letters_and_rests);
  RUN_TEST(test_sharps);
  RUN_TEST(test_octaves_span_four_to_seven);
  RUN_TEST(test_out_of_range_octaves_rejected);
  RUN_TEST(test_enharmonic_sharps_rejected);
  RUN_TEST(test_rest_takes_no_sharp);
  RUN_TEST(test_unknown_note_letter_rejected);
  RUN_TEST(test_bad_note_duration_rejected);
  RUN_TEST(test_durations_match_the_old_factory);
  RUN_TEST(test_dotted_notes_add_half);
  RUN_TEST(test_dot_accepted_on_either_side_of_the_octave);
  RUN_TEST(test_time_unit_matches_the_old_formula);
  RUN_TEST(test_duration_ms_totals_the_notes);
  RUN_TEST(test_duration_ms_matches_a_beat);
  RUN_TEST(test_doc_example_beep);
  RUN_TEST(test_doc_example_doorbell);
  RUN_TEST(test_doc_example_loose);
  RUN_TEST(test_doc_example_jackpot);
  RUN_TEST(test_builtin_r2d2_still_parses);
  RUN_TEST(test_error_index_points_at_the_offending_byte);
  RUN_TEST(test_valid_name);
  RUN_TEST(test_retitle_rewrites_the_title);
  RUN_TEST(test_retitle_prepends_to_a_two_part_string);
  RUN_TEST(test_retitle_needs_a_separator);
  return UNITY_END();
}
