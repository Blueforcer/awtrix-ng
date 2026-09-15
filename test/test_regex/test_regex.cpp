
#include <unity.h>

#include <string>

#include "core/script/Regex.h"

namespace awtrix::script {
// Observe reserved storage without adding diagnostics to the script-facing API.
struct RegexTestAccess {
  static std::size_t allocatedBytes(const Regex& re) {
    return re.prog_.capacity() + re.seen_.capacity() +
           sizeof(Regex::Thread) *
               (re.list_[0].capacity() + re.list_[1].capacity() + re.stack_.capacity());
  }
};
}

using namespace awtrix;
using script::Regex;
using script::RegexTestAccess;

void setUp() {}
void tearDown() {}

static std::string firstMatch(const char* pattern, const std::string& text) {
  Regex re;
  if (!re.compile(pattern)) return "<compile-error>";
  Regex::Span g[Regex::kMaxGroups];
  if (!re.search(text, g, Regex::kMaxGroups)) return "<no-match>";
  return text.substr(g[0].begin, g[0].end - g[0].begin);
}

static std::string group(const char* pattern, const std::string& text, int n) {
  Regex re;
  if (!re.compile(pattern)) return "<compile-error>";
  Regex::Span g[Regex::kMaxGroups];
  if (!re.search(text, g, Regex::kMaxGroups)) return "<no-match>";
  if (n >= Regex::kMaxGroups || g[n].begin < 0) return "<no-group>";
  return text.substr(g[n].begin, g[n].end - g[n].begin);
}

static void test_literal_finds_itself() {
  TEST_ASSERT_EQUAL_STRING("cat", firstMatch("cat", "concatenate").c_str());
  TEST_ASSERT_EQUAL_STRING("<no-match>", firstMatch("dog", "concatenate").c_str());
}

static void test_search_is_leftmost() {
  TEST_ASSERT_EQUAL_STRING("aa", firstMatch("aa", "xxaayyaa").c_str());
  Regex re;
  TEST_ASSERT_TRUE(re.compile("aa"));
  Regex::Span g[1];
  TEST_ASSERT_TRUE(re.search("xxaayyaa", g, 1));
  TEST_ASSERT_EQUAL_INT(2, g[0].begin);
}

static void test_dot_matches_any_byte_but_none_left() {
  TEST_ASSERT_EQUAL_STRING("a#b", firstMatch("a.b", "xa#bx").c_str());
  TEST_ASSERT_EQUAL_STRING("<no-match>", firstMatch("a.", "a").c_str());
}

static void test_empty_pattern_matches_empty_at_start() {
  Regex re;
  TEST_ASSERT_TRUE(re.compile(""));
  Regex::Span g[1];
  TEST_ASSERT_TRUE(re.search("abc", g, 1));
  TEST_ASSERT_EQUAL_INT(0, g[0].begin);
  TEST_ASSERT_EQUAL_INT(0, g[0].end);
}

static void test_escaped_metacharacters_are_literal() {
  TEST_ASSERT_EQUAL_STRING("a.b", firstMatch("a\\.b", "xa.bx").c_str());
  TEST_ASSERT_EQUAL_STRING("<no-match>", firstMatch("a\\.b", "xaXbx").c_str());
  TEST_ASSERT_EQUAL_STRING("(1)", firstMatch("\\(1\\)", "x(1)x").c_str());
}

static void test_named_classes() {
  TEST_ASSERT_EQUAL_STRING("42", firstMatch("\\d+", "abc 42 def").c_str());
  TEST_ASSERT_EQUAL_STRING("abc", firstMatch("\\w+", "  abc.").c_str());
  TEST_ASSERT_EQUAL_STRING(" \t", firstMatch("\\s+", "a \tb").c_str());
  TEST_ASSERT_EQUAL_STRING("abc ", firstMatch("\\D+", "abc 42").c_str());
}

static void test_class_ranges_and_negation() {
  TEST_ASSERT_EQUAL_STRING("f3", firstMatch("[a-f][0-9]", "zzf3zz").c_str());
  TEST_ASSERT_EQUAL_STRING("z", firstMatch("[^0-9 ]", "0 9z8").c_str());
  TEST_ASSERT_EQUAL_STRING("-", firstMatch("[-+]", "5-3").c_str());
}

static void test_class_with_named_class_inside() {
  TEST_ASSERT_EQUAL_STRING("a1", firstMatch("[\\da-z][\\d]", "  a1").c_str());
}

static void test_star_is_greedy() {
  TEST_ASSERT_EQUAL_STRING("aaa", firstMatch("a*", "aaab").c_str());
  TEST_ASSERT_EQUAL_STRING("", firstMatch("x*", "abc").c_str());
}

static void test_plus_needs_one() {
  TEST_ASSERT_EQUAL_STRING("aaa", firstMatch("a+", "baaab").c_str());
  TEST_ASSERT_EQUAL_STRING("<no-match>", firstMatch("a+", "bbb").c_str());
}

static void test_quest_is_optional() {
  TEST_ASSERT_EQUAL_STRING("ab", firstMatch("ab?", "abx").c_str());
  TEST_ASSERT_EQUAL_STRING("a", firstMatch("ab?", "axx").c_str());
}

static void test_lazy_variants_take_the_short_match() {
  TEST_ASSERT_EQUAL_STRING("<a>", firstMatch("<.*?>", "<a><b>").c_str());
  TEST_ASSERT_EQUAL_STRING("<a><b>", firstMatch("<.*>", "<a><b>").c_str());
  TEST_ASSERT_EQUAL_STRING("a", firstMatch("a+?", "aaa").c_str());
}

static void test_anchors() {
  TEST_ASSERT_EQUAL_STRING("ab", firstMatch("^ab", "abab").c_str());
  TEST_ASSERT_EQUAL_STRING("<no-match>", firstMatch("^b", "ab").c_str());
  TEST_ASSERT_EQUAL_STRING("ab", firstMatch("ab$", "abab").c_str());
  TEST_ASSERT_EQUAL_STRING("<no-match>", firstMatch("a$", "ab").c_str());
}

static void test_alternation() {
  TEST_ASSERT_EQUAL_STRING("dog", firstMatch("cat|dog", "hotdog cat").c_str());
  TEST_ASSERT_EQUAL_STRING("dog", firstMatch("(cat|dog)", "hotdog cat").c_str());
  TEST_ASSERT_EQUAL_STRING("cat", firstMatch("cat|xx", "hotdog cat").c_str());
  TEST_ASSERT_EQUAL_STRING("ab", firstMatch("a(x|b)", "zab").c_str());
}

static void test_groups_capture() {
  TEST_ASSERT_EQUAL_STRING("42", group("value=(\\d+)", "x value=42;", 1).c_str());
  TEST_ASSERT_EQUAL_STRING("value=42", group("value=(\\d+)", "x value=42;", 0).c_str());
}

static void test_two_groups() {
  const char* pat = "(\\w+)=(\\d+)";
  TEST_ASSERT_EQUAL_STRING("temp", group(pat, "a temp=21 b", 1).c_str());
  TEST_ASSERT_EQUAL_STRING("21", group(pat, "a temp=21 b", 2).c_str());
}

static void test_unused_group_reports_absent() {
  TEST_ASSERT_EQUAL_STRING("<no-group>", group("a(x)?b", "ab", 1).c_str());
}

static void test_group_under_quantifier_keeps_last_iteration() {
  TEST_ASSERT_EQUAL_STRING("c", group("(\\w)*", "abc", 1).c_str());
}

static void test_json_number_after_key() {
  const std::string body =
      "{\"stats\":{\"followingCount\":1467,\"followerCount\":29171,\"heartCount\":552291}}";
  TEST_ASSERT_EQUAL_STRING("29171", group("\"followerCount\":(\\d+)", body, 1).c_str());
}

static void test_html_scrape() {
  const std::string html = "<span class=\"count\">1.234</span>";
  TEST_ASSERT_EQUAL_STRING("1.234", group("class=\"count\">([0-9.]+)<", html, 1).c_str());
}

static void test_match_only_at_start() {
  Regex re;
  TEST_ASSERT_TRUE(re.compile("\\d+"));
  Regex::Span g[1];
  TEST_ASSERT_FALSE(re.match("a42", g, 1));
  TEST_ASSERT_TRUE(re.match("42a", g, 1));
  TEST_ASSERT_EQUAL_INT(0, g[0].begin);
  TEST_ASSERT_EQUAL_INT(2, g[0].end);
}

static void test_rejects_malformed_patterns() {
  Regex re;
  TEST_ASSERT_FALSE(re.compile("a("));
  TEST_ASSERT_FALSE(re.compile("a)"));
  TEST_ASSERT_FALSE(re.compile("[a-"));
  TEST_ASSERT_FALSE(re.compile("*a"));
  TEST_ASSERT_FALSE(re.compile("a\\"));
}

static void test_rejects_an_oversized_pattern() {
  Regex re;
  const std::string big(Regex::kMaxPattern + 1, 'a');
  TEST_ASSERT_FALSE(re.compile(big.c_str()));
}

static void test_rejects_too_many_groups() {
  std::string pat;
  for (int i = 0; i < Regex::kMaxGroups; ++i) pat += "(a)";
  Regex re;
  TEST_ASSERT_FALSE(re.compile(pat.c_str()));
}

static void test_rejects_deep_nesting() {
  std::string pat;
  for (int i = 0; i < 24; ++i) pat += "(a|";
  Regex re;
  TEST_ASSERT_FALSE(re.compile(pat.c_str()));
}

static void test_pathological_pattern_stays_linear() {
  Regex re;
  TEST_ASSERT_TRUE(re.compile("(a*)*b"));
  const std::string as(4096, 'a');
  Regex::Span g[Regex::kMaxGroups];
  TEST_ASSERT_FALSE(re.search(as, g, Regex::kMaxGroups));
}

static void test_long_subject_is_fine() {
  const std::string body = std::string(8000, 'x') + "needle=7";
  TEST_ASSERT_EQUAL_STRING("7", group("needle=(\\d)", body, 1).c_str());
}

static void test_json_pattern_has_small_reusable_scratch() {
  Regex re;
  TEST_ASSERT_TRUE(re.compile("\"temperature\":([0-9.]+)"));
  const std::size_t bytes = RegexTestAccess::allocatedBytes(re);
  TEST_ASSERT_LESS_OR_EQUAL_UINT32(2048, bytes);
  Regex::Span g[Regex::kMaxGroups];
  for (int i = 0; i < 300; ++i) {
    TEST_ASSERT_TRUE(re.search("{\"temperature\":21.5}", g, Regex::kMaxGroups));
    TEST_ASSERT_EQUAL_INT(15, g[1].begin);
    TEST_ASSERT_EQUAL_INT(19, g[1].end);
  }
  TEST_ASSERT_EQUAL_UINT32(bytes, RegexTestAccess::allocatedBytes(re));
}

static void test_largest_program_keeps_all_literal_states() {
  Regex re;
  const std::string pattern(250, 'a');  // Fills all 512 bytecode bytes.
  TEST_ASSERT_TRUE(re.compile(pattern));
  const std::size_t bytes = RegexTestAccess::allocatedBytes(re);
  TEST_ASSERT_LESS_OR_EQUAL_UINT32(20000, bytes);
  Regex::Span g[1];
  TEST_ASSERT_TRUE(re.search("x" + pattern, g, 1));
  TEST_ASSERT_EQUAL_INT(1, g[0].begin);
  TEST_ASSERT_EQUAL_INT(251, g[0].end);
  TEST_ASSERT_FALSE(re.search(std::string(249, 'a'), g, 1));
  TEST_ASSERT_EQUAL_UINT32(bytes, RegexTestAccess::allocatedBytes(re));
}

static void test_maximum_pattern_length_with_wildcards() {
  Regex re;
  TEST_ASSERT_TRUE(re.compile(std::string(Regex::kMaxPattern, '.')));
  const std::size_t bytes = RegexTestAccess::allocatedBytes(re);
  TEST_ASSERT_LESS_OR_EQUAL_UINT32(20000, bytes);
  Regex::Span g[1];
  TEST_ASSERT_TRUE(re.match(std::string(Regex::kMaxPattern, 'x'), g, 1));
  TEST_ASSERT_EQUAL_INT(256, g[0].end);
  TEST_ASSERT_FALSE(re.match(std::string(Regex::kMaxPattern - 1, 'x'), g, 1));
  TEST_ASSERT_EQUAL_UINT32(bytes, RegexTestAccess::allocatedBytes(re));
}

static void test_many_splits_keep_greedy_order_without_scratch_growth() {
  Regex re;
  std::string pattern;
  for (int i = 0; i < 100; ++i) pattern += "a?";  // Also fills 512 bytecode bytes.
  TEST_ASSERT_TRUE(re.compile(pattern));
  const std::size_t bytes = RegexTestAccess::allocatedBytes(re);
  Regex::Span g[1];
  TEST_ASSERT_TRUE(re.match(std::string(100, 'a'), g, 1));
  TEST_ASSERT_EQUAL_INT(100, g[0].end);
  TEST_ASSERT_TRUE(re.match("", g, 1));
  TEST_ASSERT_EQUAL_INT(0, g[0].end);
  TEST_ASSERT_EQUAL_UINT32(bytes, RegexTestAccess::allocatedBytes(re));
}

static void test_all_capture_slots_survive_nullable_branches() {
  Regex re;
  TEST_ASSERT_TRUE(re.compile("((a|b)*)(c?)(d?)(e?)(f?)(g?)"));
  TEST_ASSERT_EQUAL_INT(Regex::kMaxGroups, re.groupCount());
  const std::size_t bytes = RegexTestAccess::allocatedBytes(re);
  Regex::Span g[Regex::kMaxGroups];
  TEST_ASSERT_TRUE(re.match("abacdefg", g, Regex::kMaxGroups));
  TEST_ASSERT_EQUAL_INT(8, g[0].end);
  TEST_ASSERT_EQUAL_INT(0, g[1].begin);
  TEST_ASSERT_EQUAL_INT(3, g[1].end);
  TEST_ASSERT_EQUAL_INT(2, g[2].begin);
  TEST_ASSERT_EQUAL_INT(3, g[2].end);
  for (int i = 3; i < Regex::kMaxGroups; ++i) {
    TEST_ASSERT_EQUAL_INT(i, g[i].begin);
    TEST_ASSERT_EQUAL_INT(i + 1, g[i].end);
  }
  TEST_ASSERT_TRUE(re.match("", g, Regex::kMaxGroups));
  TEST_ASSERT_EQUAL_INT(-1, g[2].begin);
  TEST_ASSERT_EQUAL_UINT32(bytes, RegexTestAccess::allocatedBytes(re));
}

static void test_recompile_releases_large_and_invalid_pattern_storage() {
  Regex re;
  TEST_ASSERT_TRUE(re.compile(std::string(250, 'a')));
  TEST_ASSERT_TRUE(re.compile("abc"));
  TEST_ASSERT_LESS_OR_EQUAL_UINT32(1024, RegexTestAccess::allocatedBytes(re));
  Regex::Span g[1];
  TEST_ASSERT_TRUE(re.search("xabc", g, 1));
  TEST_ASSERT_EQUAL_INT(1, g[0].begin);
  TEST_ASSERT_FALSE(re.compile("a("));
  TEST_ASSERT_FALSE(re.ok());
  TEST_ASSERT_EQUAL_UINT32(0, RegexTestAccess::allocatedBytes(re));
  TEST_ASSERT_FALSE(re.search("abc", g, 1));
  TEST_ASSERT_TRUE(re.compile("a"));
  TEST_ASSERT_TRUE(re.search("a", g, 1));
}

static void test_views_preserve_embedded_nul_and_slice_boundaries() {
  Regex re;
  const char pattern[] = {'a', '\0', 'b', '?'};
  const char text[] = {'x', 'a', '\0', 'b', 'y'};
  TEST_ASSERT_TRUE(re.compile(std::string_view(pattern, 3)));
  Regex::Span g[1];
  TEST_ASSERT_TRUE(re.search(std::string_view(text, sizeof(text)), g, 1));
  TEST_ASSERT_EQUAL_INT(1, g[0].begin);
  TEST_ASSERT_EQUAL_INT(4, g[0].end);
  TEST_ASSERT_FALSE(re.match(std::string_view(text + 1, 2), g, 1));
  TEST_ASSERT_TRUE(re.match(std::string_view(text + 1, 3), g, 1));
  TEST_ASSERT_EQUAL_INT(3, g[0].end);
}

static void test_subject_limit_and_search_from_end_preserve_offsets() {
  Regex re;
  TEST_ASSERT_TRUE(re.compile("(z)$"));
  std::string text(Regex::kMaxInput, 'a');
  text.back() = 'z';
  Regex::Span g[2];
  TEST_ASSERT_TRUE(re.searchFrom(text, text.size() - 1, g, 2));
  TEST_ASSERT_EQUAL_INT(31999, g[1].begin);
  TEST_ASSERT_EQUAL_INT(32000, g[1].end);
  TEST_ASSERT_FALSE(re.searchFrom(text, text.size() + 1, g, 2));
  text += 'z';
  TEST_ASSERT_FALSE(re.search(text, g, 2));
  TEST_ASSERT_TRUE(re.compile("$"));
  TEST_ASSERT_TRUE(re.searchFrom("abc", 3, g, 1));
  TEST_ASSERT_EQUAL_INT(3, g[0].begin);
  TEST_ASSERT_EQUAL_INT(3, g[0].end);
}

int main(int, char**) {
  UNITY_BEGIN();
  RUN_TEST(test_literal_finds_itself);
  RUN_TEST(test_search_is_leftmost);
  RUN_TEST(test_dot_matches_any_byte_but_none_left);
  RUN_TEST(test_empty_pattern_matches_empty_at_start);
  RUN_TEST(test_escaped_metacharacters_are_literal);
  RUN_TEST(test_named_classes);
  RUN_TEST(test_class_ranges_and_negation);
  RUN_TEST(test_class_with_named_class_inside);
  RUN_TEST(test_star_is_greedy);
  RUN_TEST(test_plus_needs_one);
  RUN_TEST(test_quest_is_optional);
  RUN_TEST(test_lazy_variants_take_the_short_match);
  RUN_TEST(test_anchors);
  RUN_TEST(test_alternation);
  RUN_TEST(test_groups_capture);
  RUN_TEST(test_two_groups);
  RUN_TEST(test_unused_group_reports_absent);
  RUN_TEST(test_group_under_quantifier_keeps_last_iteration);
  RUN_TEST(test_json_number_after_key);
  RUN_TEST(test_html_scrape);
  RUN_TEST(test_match_only_at_start);
  RUN_TEST(test_rejects_malformed_patterns);
  RUN_TEST(test_rejects_an_oversized_pattern);
  RUN_TEST(test_rejects_too_many_groups);
  RUN_TEST(test_rejects_deep_nesting);
  RUN_TEST(test_pathological_pattern_stays_linear);
  RUN_TEST(test_long_subject_is_fine);
  RUN_TEST(test_json_pattern_has_small_reusable_scratch);
  RUN_TEST(test_largest_program_keeps_all_literal_states);
  RUN_TEST(test_maximum_pattern_length_with_wildcards);
  RUN_TEST(test_many_splits_keep_greedy_order_without_scratch_growth);
  RUN_TEST(test_all_capture_slots_survive_nullable_branches);
  RUN_TEST(test_recompile_releases_large_and_invalid_pattern_storage);
  RUN_TEST(test_views_preserve_embedded_nul_and_slice_boundaries);
  RUN_TEST(test_subject_limit_and_search_from_end_preserve_offsets);
  return UNITY_END();
}
