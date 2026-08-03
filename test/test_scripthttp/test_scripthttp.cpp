
#include <unity.h>

#include <algorithm>
#include <cstddef>
#include <initializer_list>
#include <string>

#include "core/script/HttpBodyFilter.h"
#include "core/script/HttpHeaders.h"
#include "core/script/ScriptServices.h"

using namespace awtrix;

void setUp() {}
void tearDown() {}

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

static void test_rejects_an_oversized_line() {
  const std::string value(script::kMaxHttpHeaderBytes, 'x');
  script::HttpHeaders h;
  TEST_ASSERT_FALSE(script::parseHeaderBlock("X-Big: " + value, h));

  const std::string fits(script::kMaxHttpHeaderBytes - 7, 'x');
  TEST_ASSERT_TRUE(script::parseHeaderBlock("X-Big: " + fits, h));
  TEST_ASSERT_EQUAL_UINT32(1u, static_cast<uint32_t>(h.size()));
}

static void test_rejects_too_many_headers() {
  std::string full;
  for (std::size_t i = 0; i < script::kMaxHttpHeaders; ++i) {
    if (!full.empty()) full.push_back(script::kHeaderSeparator);
    full += "X-" + std::to_string(i) + ": v";
  }

  script::HttpHeaders h;
  TEST_ASSERT_TRUE(script::parseHeaderBlock(full, h));
  TEST_ASSERT_EQUAL_UINT32(static_cast<uint32_t>(script::kMaxHttpHeaders),
                           static_cast<uint32_t>(h.size()));

  TEST_ASSERT_FALSE(
      script::parseHeaderBlock(full + script::kHeaderSeparator + "X-Over: v", h));
  TEST_ASSERT_EQUAL_UINT32(static_cast<uint32_t>(script::kMaxHttpHeaders),
                           static_cast<uint32_t>(h.size()));
}

static void test_filtered_headers_do_not_count_towards_the_cap() {
  std::string full = block({"Host: a", "Connection: close"});
  for (std::size_t i = 0; i < script::kMaxHttpHeaders; ++i) {
    full.push_back(script::kHeaderSeparator);
    full += "X-" + std::to_string(i) + ": v";
  }

  script::HttpHeaders h;
  TEST_ASSERT_TRUE(script::parseHeaderBlock(full, h));
  TEST_ASSERT_EQUAL_UINT32(static_cast<uint32_t>(script::kMaxHttpHeaders),
                           static_cast<uint32_t>(h.size()));
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
  RUN_TEST(test_rejects_an_oversized_line);
  RUN_TEST(test_rejects_too_many_headers);
  RUN_TEST(test_filtered_headers_do_not_count_towards_the_cap);
  RUN_TEST(test_filter_plain_mode_keeps_the_first_cap_bytes);
  RUN_TEST(test_filter_plain_mode_short_body_is_kept_whole);
  RUN_TEST(test_filter_window_starts_at_the_match_and_includes_the_needle);
  RUN_TEST(test_filter_finds_a_needle_straddling_a_chunk_boundary);
  RUN_TEST(test_filter_window_collects_across_chunks_then_discards);
  RUN_TEST(test_filter_no_match_yields_an_empty_body);
  RUN_TEST(test_filter_stream_may_end_before_the_window_fills);
  RUN_TEST(test_filter_keep_defaults_and_clamps);
  RUN_TEST(test_filter_match_at_the_very_first_byte);
  return UNITY_END();
}
