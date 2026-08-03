
#include <unity.h>

#include <string>

#include "core/script/Regex.h"

using namespace awtrix;
using script::Regex;

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
  return UNITY_END();
}
