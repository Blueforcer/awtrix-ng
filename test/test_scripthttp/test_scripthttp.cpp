
#include <unity.h>

#include <algorithm>
#include <cstddef>
#include <initializer_list>
#include <string>

#include "core/script/HttpBodyFilter.h"
#include "core/script/HttpHeaders.h"
#include "core/script/ScriptHeapTesting.h"
#include "core/script/ScriptServices.h"

using namespace awtrix;

void setUp() {}
void tearDown() { script::heap::testing::resetGrowthBudget(); }

static std::string block(std::initializer_list<std::string> entries) {
  std::string out;
  for (const auto& e : entries) {
    if (!out.empty()) out.push_back(script::kHeaderSeparator);
    out += e;
  }
  return out;
}

static std::string valueOf(const script::HttpHeaders& h, const std::string& name) {
  for (const auto& kv : h)
    if (kv.first == name) return kv.second;
  return "<missing>";
}

static void test_method_normalises_and_rejects() {
  std::string out;
  TEST_ASSERT_TRUE(script::normalizeMethod("get", out));
  TEST_ASSERT_EQUAL_STRING("GET", out.c_str());
  TEST_ASSERT_TRUE(script::normalizeMethod("PaTcH", out));
  TEST_ASSERT_EQUAL_STRING("PATCH", out.c_str());

  TEST_ASSERT_TRUE(script::normalizeMethod("", out));
  TEST_ASSERT_EQUAL_STRING("GET", out.c_str());

  TEST_ASSERT_FALSE(script::normalizeMethod("TRACE", out));
  TEST_ASSERT_FALSE(script::normalizeMethod("CONNECT", out));
  TEST_ASSERT_FALSE(script::normalizeMethod("GET /evil HTTP/1.1", out));
}

static void test_parses_a_plain_block() {
  script::HttpHeaders h;
  TEST_ASSERT_TRUE(script::parseHeaderBlock(
      block({"Authorization: Bearer TOK", "Accept: application/json"}), h));
  TEST_ASSERT_EQUAL_UINT32(2u, static_cast<uint32_t>(h.size()));
  TEST_ASSERT_EQUAL_STRING("Bearer TOK", valueOf(h, "Authorization").c_str());
  TEST_ASSERT_EQUAL_STRING("application/json", valueOf(h, "Accept").c_str());
}

static void test_skips_empty_entries() {
  script::HttpHeaders h;
  TEST_ASSERT_TRUE(script::parseHeaderBlock(block({"A: 1", "", "B: 2"}), h));
  TEST_ASSERT_EQUAL_UINT32(2u, static_cast<uint32_t>(h.size()));
  TEST_ASSERT_EQUAL_STRING("1", valueOf(h, "A").c_str());
  TEST_ASSERT_EQUAL_STRING("2", valueOf(h, "B").c_str());
}

static void test_empty_block_is_valid_and_empty() {
  script::HttpHeaders h;
  h.emplace_back("stale", "entry");
  TEST_ASSERT_TRUE(script::parseHeaderBlock("", h));
  TEST_ASSERT_TRUE(h.empty());
}

static void test_values_keep_their_inner_spacing() {
  script::HttpHeaders h;
  TEST_ASSERT_TRUE(script::parseHeaderBlock("X-Note:   two words   ", h));
  TEST_ASSERT_EQUAL_STRING("two words", valueOf(h, "X-Note").c_str());
}

static void test_empty_value_survives() {
  script::HttpHeaders h;
  TEST_ASSERT_TRUE(script::parseHeaderBlock("X-Empty:", h));
  TEST_ASSERT_EQUAL_UINT32(1u, static_cast<uint32_t>(h.size()));
  TEST_ASSERT_EQUAL_STRING("", valueOf(h, "X-Empty").c_str());
}

static void test_drops_transport_owned_headers_case_insensitively() {
  script::HttpHeaders h;
  TEST_ASSERT_TRUE(script::parseHeaderBlock(
      block({"host: evil.example", "CONTENT-LENGTH: 0", "Transfer-Encoding: chunked",
             "Connection: close", "X-Keep: yes"}),
      h));
  TEST_ASSERT_EQUAL_UINT32(1u, static_cast<uint32_t>(h.size()));
  TEST_ASSERT_EQUAL_STRING("yes", valueOf(h, "X-Keep").c_str());

  TEST_ASSERT_FALSE(script::headerAllowed("Host"));
  TEST_ASSERT_TRUE(script::headerAllowed("Authorization"));
}

static void test_rejects_injected_control_characters() {
  script::HttpHeaders h;
  TEST_ASSERT_FALSE(script::parseHeaderBlock("X-A: v\r\nEvil: yes", h));
  TEST_ASSERT_FALSE(script::parseHeaderBlock("X-A: v\nEvil: yes", h));
  TEST_ASSERT_FALSE(script::parseHeaderBlock("X-A: bad\rvalue", h));
  TEST_ASSERT_FALSE(script::parseHeaderBlock("X-A: bad\tvalue", h));
}

static void test_rejects_malformed_names() {
  script::HttpHeaders h;
  TEST_ASSERT_FALSE(script::parseHeaderBlock("no-colon-here", h));
  TEST_ASSERT_FALSE(script::parseHeaderBlock(": empty-name", h));
  TEST_ASSERT_FALSE(script::parseHeaderBlock("two words: v", h));
  TEST_ASSERT_FALSE(script::parseHeaderBlock("na@me: v", h));
}

static void test_failed_parse_leaves_out_untouched() {
  script::HttpHeaders h;
  h.emplace_back("stale", "entry");
  TEST_ASSERT_FALSE(script::parseHeaderBlock("two words: v", h));
  TEST_ASSERT_EQUAL_UINT32(1u, static_cast<uint32_t>(h.size()));
  TEST_ASSERT_EQUAL_STRING("entry", valueOf(h, "stale").c_str());
}

static void test_a_long_header_line_parses() {
  const std::string value(2000, 'x');
  script::HttpHeaders h;
  TEST_ASSERT_TRUE(script::parseHeaderBlock("X-Big: " + value, h));
  TEST_ASSERT_EQUAL_STRING(value.c_str(), valueOf(h, "X-Big").c_str());
}

static void test_more_than_eight_headers_parse() {
  std::string full;
  for (int i = 0; i < 20; ++i) {
    if (!full.empty()) full.push_back(script::kHeaderSeparator);
    full += "X-N" + std::to_string(i) + ": v";
  }

  script::HttpHeaders h;
  TEST_ASSERT_TRUE(script::parseHeaderBlock(full, h));
  TEST_ASSERT_EQUAL_UINT32(20u, static_cast<uint32_t>(h.size()));
}

static void test_plain_requests_pass_a_heap_too_small_for_tls() {
  TEST_ASSERT_TRUE(script::fetchFits(false, 8 * 1024, 8 * 1024));
}

static void test_tls_needs_the_whole_working_set() {
  TEST_ASSERT_TRUE(script::fetchFits(true, script::kTlsWorkingSetBytes, 64 * 1024));
  TEST_ASSERT_FALSE(script::fetchFits(true, script::kTlsWorkingSetBytes - 1, 64 * 1024));
}

static void test_tls_needs_a_contiguous_record_buffer() {
  TEST_ASSERT_TRUE(
      script::fetchFits(true, 128 * 1024, script::kTlsContiguousBytes));
  TEST_ASSERT_FALSE(
      script::fetchFits(true, 128 * 1024, script::kTlsContiguousBytes - 1));
}

static void feedChunked(script::HttpBodyFilter& f, const std::string& body,
                        std::size_t chunk) {
  for (std::size_t i = 0; i < body.size(); i += chunk)
    f.feed(body.data() + i, std::min(chunk, body.size() - i));
}

static void test_filter_plain_mode_keeps_the_first_cap_bytes() {
  script::HttpBodyFilter f;
  f.begin("", 0, 8);
  feedChunked(f, "0123456789abcdef", 5);
  TEST_ASSERT_TRUE(f.matched());
  TEST_ASSERT_TRUE(f.done());
  TEST_ASSERT_EQUAL_STRING("01234567", f.body().c_str());
}

static void test_filter_plain_mode_short_body_is_kept_whole() {
  script::HttpBodyFilter f;
  f.begin("", 0, 8192);
  feedChunked(f, "{\"a\":1}", 3);
  TEST_ASSERT_FALSE(f.done());
  TEST_ASSERT_EQUAL_STRING("{\"a\":1}", f.body().c_str());
}

static void test_filter_window_starts_at_the_match_and_includes_the_needle() {
  script::HttpBodyFilter f;
  f.begin("\"n\":", 8, 8192);
  feedChunked(f, "{\"pad\":true,\"n\":40444,\"z\":0}", 7);
  TEST_ASSERT_TRUE(f.matched());
  TEST_ASSERT_EQUAL_STRING("\"n\":4044", f.body().c_str());
}

static void test_filter_finds_a_needle_straddling_a_chunk_boundary() {
  const std::string body = "aaaaaNEEDLEbbbbb";
  for (std::size_t chunk = 1; chunk <= body.size(); ++chunk) {
    script::HttpBodyFilter f;
    f.begin("NEEDLE", 11, 8192);
    feedChunked(f, body, chunk);
    TEST_ASSERT_TRUE(f.matched());
    TEST_ASSERT_EQUAL_STRING("NEEDLEbbbbb", f.body().c_str());
  }
}

// A decoy earlier in the stream shares the needle's prefix but not its full text; only a search
// that matches the needle in full, not just a leading slice of it, skips the decoy and finds the
// real occurrence. Small chunks also make the tail carry-over reassemble the needle piece by
// piece across many feeds.
static void test_filter_matches_the_full_needle_not_a_shared_prefix() {
  const std::string needle = std::string(90, 'A') + "TAG";
  const std::string decoy = std::string(200, 'A') + "no-match-here";
  const std::string real = needle + "XYZ";
  script::HttpBodyFilter f;
  f.begin(needle, needle.size() + 3, 8192);
  feedChunked(f, decoy + real, 7);
  TEST_ASSERT_TRUE(f.matched());
  TEST_ASSERT_EQUAL_STRING(real.c_str(), f.body().c_str());
}

static void test_filter_window_collects_across_chunks_then_discards() {
  script::HttpBodyFilter f;
  f.begin("k=", 6, 8192);
  feedChunked(f, std::string(1000, 'x') + "k=12345678" + std::string(1000, 'y'), 3);
  TEST_ASSERT_TRUE(f.done());
  TEST_ASSERT_EQUAL_STRING("k=1234", f.body().c_str());
}

static void test_filter_no_match_yields_an_empty_body() {
  script::HttpBodyFilter f;
  f.begin("\"missing\":", 0, 8192);
  feedChunked(f, std::string(4096, 'a'), 100);
  TEST_ASSERT_FALSE(f.matched());
  TEST_ASSERT_EQUAL_STRING("", f.body().c_str());
}

static void test_filter_stream_may_end_before_the_window_fills() {
  script::HttpBodyFilter f;
  f.begin("v=", 100, 8192);
  feedChunked(f, "pad pad v=42", 4);
  TEST_ASSERT_TRUE(f.matched());
  TEST_ASSERT_FALSE(f.done());
  TEST_ASSERT_EQUAL_STRING("v=42", f.body().c_str());
}

static void test_filter_keep_defaults_and_clamps() {
  script::HttpBodyFilter f;
  f.begin("x", 0, 8192);
  feedChunked(f, "x" + std::string(1000, 'p'), 64);
  TEST_ASSERT_EQUAL_UINT32(static_cast<uint32_t>(script::kDefaultHttpKeep),
                           static_cast<uint32_t>(f.body().size()));

  script::HttpBodyFilter g;
  g.begin("x", 100, 10);
  feedChunked(g, "x" + std::string(1000, 'p'), 64);
  TEST_ASSERT_EQUAL_UINT32(10u, static_cast<uint32_t>(g.body().size()));
}

// A short answer costs what it is. Nothing is set aside toward the size the request named, so a
// script asking for room it does not use pays nothing for the asking.
static void test_a_short_answer_reserves_nothing_toward_the_cap() {
  script::HttpBodyFilter f;
  f.begin("", 0, 64 * 1024);
  feedChunked(f, "{\"a\":1}", 3);
  TEST_ASSERT_EQUAL_UINT32(7u, static_cast<uint32_t>(f.body().size()));
  TEST_ASSERT_TRUE(f.body().capacity() < 64u);
}

// The kept bytes grow by a bounded step rather than doubling, so collecting a large answer never
// asks for a block twice the size of what is already held.
static void test_the_answer_grows_by_a_bounded_step() {
  script::HttpBodyFilter f;
  f.begin("", 0, 256 * 1024);
  feedChunked(f, std::string(20000, 'a'), 1000);
  TEST_ASSERT_FALSE(f.outOfRoom());
  TEST_ASSERT_EQUAL_UINT32(20000u, static_cast<uint32_t>(f.body().size()));
  TEST_ASSERT_TRUE(f.body().capacity() - f.body().size() <= script::kBodyGrowStepBytes);
}

// An answer that outgrows the device stops in the filter, which reports it, rather than in the
// allocator. The caller turns that into a failed request instead of a half answer.
static void test_an_answer_that_outgrows_the_device_is_refused() {
  script::heap::testing::setGrowthBudget(2048);
  script::HttpBodyFilter f;
  f.begin("", 0, 256 * 1024);
  feedChunked(f, std::string(8000, 'a'), 1000);
  TEST_ASSERT_TRUE(f.outOfRoom());
  TEST_ASSERT_TRUE(f.done());
  TEST_ASSERT_TRUE(f.body().size() < 8000u);
}

// What is already held is allocated, so the free heap the step is measured against has it
// subtracted already. Charging it a second time refuses a body the device can plainly hold: a
// 12288 budget takes a 12000-byte answer, even though the last step is copied out of a buffer
// most of that size. Measured on hardware - a board without PSRAM could not collect a 32 KB
// response against a 65 KB budget while the held bytes were counted twice.
static void test_only_the_new_block_is_weighed_against_the_budget() {
  script::heap::testing::setGrowthBudget(12 * 1024);
  script::HttpBodyFilter f;
  f.begin("", 0, 256 * 1024);
  feedChunked(f, std::string(12000, 'a'), 1000);
  TEST_ASSERT_FALSE(f.outOfRoom());
  TEST_ASSERT_EQUAL_UINT32(12000u, static_cast<uint32_t>(f.body().size()));
  TEST_ASSERT_TRUE(f.body().capacity() <= 12u * 1024u);
}

// A script names the size it wants and gets it, as long as the memory to collect it is there.
static void test_a_cap_that_fits_is_passed_through() {
  script::heap::testing::setGrowthBudget(64 * 1024);
  TEST_ASSERT_EQUAL_UINT32(8u * 1024u, static_cast<uint32_t>(script::httpBodyCap(8 * 1024)));
  TEST_ASSERT_EQUAL_UINT32(64u * 1024u, static_cast<uint32_t>(script::httpBodyCap(64 * 1024)));
}

// A script may ask for a quarter of a megabyte. Collecting it would run the heap out and the
// append that failed would abort the firmware, so the figure is met with what is actually there.
static void test_a_cap_beyond_the_room_available_is_brought_down_to_it() {
  script::heap::testing::setGrowthBudget(20 * 1024);
  TEST_ASSERT_EQUAL_UINT32(20u * 1024u, static_cast<uint32_t>(script::httpBodyCap(256 * 1024)));

  script::heap::testing::setGrowthBudget(0);
  TEST_ASSERT_EQUAL_UINT32(0u, static_cast<uint32_t>(script::httpBodyCap(256 * 1024)));
}

// With nothing to spare the filter keeps nothing, and a search reports no match rather than
// handing the script a truncated window it would read as the real answer.
static void test_with_no_room_a_search_reports_no_match() {
  script::heap::testing::setGrowthBudget(0);
  script::HttpBodyFilter f;
  f.begin("\"n\":", 64, script::httpBodyCap(256 * 1024));
  feedChunked(f, "{\"n\":40444}", 4);
  TEST_ASSERT_FALSE(f.matched());
  TEST_ASSERT_EQUAL_STRING("", f.body().c_str());
}

// The clamp is what the filter is armed with, so the body it collects stops at the room there
// was rather than at the figure the script named.
static void test_the_collected_body_stops_at_the_room_available() {
  script::heap::testing::setGrowthBudget(16);
  script::HttpBodyFilter f;
  f.begin("", 0, script::httpBodyCap(64 * 1024));
  feedChunked(f, std::string(4096, 'a'), 100);
  TEST_ASSERT_EQUAL_UINT32(16u, static_cast<uint32_t>(f.body().size()));
}

static void test_filter_match_at_the_very_first_byte() {
  script::HttpBodyFilter f;
  f.begin("{", 4, 8192);
  feedChunked(f, "{\"a\":1}", 1);
  TEST_ASSERT_TRUE(f.matched());
  TEST_ASSERT_EQUAL_STRING("{\"a\"", f.body().c_str());
}

int main(int, char**) {
  UNITY_BEGIN();
  RUN_TEST(test_plain_requests_pass_a_heap_too_small_for_tls);
  RUN_TEST(test_tls_needs_the_whole_working_set);
  RUN_TEST(test_tls_needs_a_contiguous_record_buffer);
  RUN_TEST(test_method_normalises_and_rejects);
  RUN_TEST(test_parses_a_plain_block);
  RUN_TEST(test_skips_empty_entries);
  RUN_TEST(test_empty_block_is_valid_and_empty);
  RUN_TEST(test_values_keep_their_inner_spacing);
  RUN_TEST(test_empty_value_survives);
  RUN_TEST(test_drops_transport_owned_headers_case_insensitively);
  RUN_TEST(test_rejects_injected_control_characters);
  RUN_TEST(test_rejects_malformed_names);
  RUN_TEST(test_failed_parse_leaves_out_untouched);
  RUN_TEST(test_a_long_header_line_parses);
  RUN_TEST(test_more_than_eight_headers_parse);
  RUN_TEST(test_filter_plain_mode_keeps_the_first_cap_bytes);
  RUN_TEST(test_filter_plain_mode_short_body_is_kept_whole);
  RUN_TEST(test_filter_window_starts_at_the_match_and_includes_the_needle);
  RUN_TEST(test_filter_finds_a_needle_straddling_a_chunk_boundary);
  RUN_TEST(test_filter_matches_the_full_needle_not_a_shared_prefix);
  RUN_TEST(test_filter_window_collects_across_chunks_then_discards);
  RUN_TEST(test_filter_no_match_yields_an_empty_body);
  RUN_TEST(test_filter_stream_may_end_before_the_window_fills);
  RUN_TEST(test_filter_keep_defaults_and_clamps);
  RUN_TEST(test_a_short_answer_reserves_nothing_toward_the_cap);
  RUN_TEST(test_the_answer_grows_by_a_bounded_step);
  RUN_TEST(test_an_answer_that_outgrows_the_device_is_refused);
  RUN_TEST(test_filter_match_at_the_very_first_byte);
  RUN_TEST(test_only_the_new_block_is_weighed_against_the_budget);
  RUN_TEST(test_a_cap_that_fits_is_passed_through);
  RUN_TEST(test_a_cap_beyond_the_room_available_is_brought_down_to_it);
  RUN_TEST(test_with_no_room_a_search_reports_no_match);
  RUN_TEST(test_the_collected_body_stops_at_the_room_available);
  return UNITY_END();
}
