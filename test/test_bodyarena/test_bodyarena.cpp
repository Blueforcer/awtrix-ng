
#include <unity.h>

#include <string>

#include "transport/http/BodyArena.h"

using namespace awtrix;

void setUp() {}
void tearDown() {}

static void test_boot_init_then_simple_body() {
  BodyArena a;
  TEST_ASSERT_TRUE(a.init(64));
  TEST_ASSERT_TRUE(a.ready());

  a.open(64);
  a.append("{\"x\":1}", 7);
  a.finish();
  TEST_ASSERT_EQUAL(static_cast<int>(BodyArena::State::Done), static_cast<int>(a.state()));
  TEST_ASSERT_EQUAL_STRING("{\"x\":1}", std::string(a.view()).c_str());
}

static void test_chunked_append_reassembles() {
  BodyArena a;
  TEST_ASSERT_TRUE(a.init(64));
  a.open(64);
  a.append("abc", 3);
  a.append("def", 3);
  a.append("g", 1);
  a.finish();
  TEST_ASSERT_EQUAL_STRING("abcdefg", std::string(a.view()).c_str());
}

static void test_reuse_across_requests() {
  BodyArena a;
  TEST_ASSERT_TRUE(a.init(32));

  a.open(32);
  a.append("first", 5);
  a.finish();
  TEST_ASSERT_EQUAL_STRING("first", std::string(a.view()).c_str());

  a.open(32);
  a.append("2", 1);
  a.finish();
  TEST_ASSERT_EQUAL_STRING("2", std::string(a.view()).c_str());
}

static void test_route_cap_overflows_without_truncating() {
  BodyArena a;
  TEST_ASSERT_TRUE(a.init(64));
  a.open(8);
  a.append("123456789", 9);
  a.finish();
  TEST_ASSERT_EQUAL(static_cast<int>(BodyArena::State::Overflow),
                    static_cast<int>(a.state()));
  TEST_ASSERT_EQUAL_UINT32(0u, static_cast<uint32_t>(a.view().size()));
}

static void test_overflow_on_the_boundary_chunk() {
  BodyArena a;
  TEST_ASSERT_TRUE(a.init(64));
  a.open(4);
  a.append("123", 3);
  a.append("45", 2);
  a.finish();
  TEST_ASSERT_EQUAL(static_cast<int>(BodyArena::State::Overflow),
                    static_cast<int>(a.state()));
}

static void test_exactly_at_cap_is_fine() {
  BodyArena a;
  TEST_ASSERT_TRUE(a.init(64));
  a.open(4);
  a.append("1234", 4);
  a.finish();
  TEST_ASSERT_EQUAL_STRING("1234", std::string(a.view()).c_str());
}

static void test_cap_over_capacity_is_clamped() {
  BodyArena a;
  TEST_ASSERT_TRUE(a.init(8));
  a.open(1024);
  a.append("123456789", 9);
  a.finish();
  TEST_ASSERT_EQUAL(static_cast<int>(BodyArena::State::Overflow),
                    static_cast<int>(a.state()));
}

static void test_reset_abandons_an_aborted_request() {
  BodyArena a;
  TEST_ASSERT_TRUE(a.init(32));
  a.open(32);
  a.append("half a bo", 9);
  a.reset();
  TEST_ASSERT_EQUAL(static_cast<int>(BodyArena::State::Idle), static_cast<int>(a.state()));
  TEST_ASSERT_EQUAL_UINT32(0u, static_cast<uint32_t>(a.view().size()));

  a.open(32);
  a.append("next", 4);
  a.finish();
  TEST_ASSERT_EQUAL_STRING("next", std::string(a.view()).c_str());
}

static void test_view_is_empty_unless_done() {
  BodyArena a;
  TEST_ASSERT_TRUE(a.init(32));
  TEST_ASSERT_EQUAL_UINT32(0u, static_cast<uint32_t>(a.view().size()));
  a.open(32);
  a.append("data", 4);
  TEST_ASSERT_EQUAL_UINT32(0u, static_cast<uint32_t>(a.view().size()));
}

static void test_empty_body_is_done_and_empty() {
  BodyArena a;
  TEST_ASSERT_TRUE(a.init(32));
  a.open(32);
  a.finish();
  TEST_ASSERT_EQUAL(static_cast<int>(BodyArena::State::Done), static_cast<int>(a.state()));
  TEST_ASSERT_EQUAL_UINT32(0u, static_cast<uint32_t>(a.view().size()));
}

static void test_release_makes_arena_absent() {
  BodyArena a;
  TEST_ASSERT_TRUE(a.init(64));
  a.open(64);
  a.append("data", 4);
  a.finish();

  a.release();
  TEST_ASSERT_FALSE(a.ready());
  TEST_ASSERT_EQUAL(static_cast<int>(BodyArena::State::Idle), static_cast<int>(a.state()));
  TEST_ASSERT_EQUAL_UINT32(0u, static_cast<uint32_t>(a.view().size()));
}

static void test_release_on_absent_arena_is_a_noop() {
  BodyArena a;
  TEST_ASSERT_FALSE(a.ready());
  a.release();
  a.release();
  TEST_ASSERT_FALSE(a.ready());
}

static void test_transient_lazy_init_release_cycle() {
  BodyArena a;
  TEST_ASSERT_TRUE(a.init(16 * 1024));
  a.open(16 * 1024);
  a.append("bigsource", 9);
  a.finish();
  TEST_ASSERT_EQUAL_STRING("bigsource", std::string(a.view()).c_str());
  a.release();
  TEST_ASSERT_FALSE(a.ready());

  TEST_ASSERT_TRUE(a.init(16 * 1024));
  a.open(16 * 1024);
  a.append("again", 5);
  a.finish();
  TEST_ASSERT_EQUAL_STRING("again", std::string(a.view()).c_str());
  a.release();
}

static void test_content_length_sizes_the_allocation() {
  TEST_ASSERT_EQUAL_UINT32(900u, static_cast<uint32_t>(arenaCapacityFor(900, 32 * 1024)));
  TEST_ASSERT_EQUAL_UINT32(32u * 1024u,
                           static_cast<uint32_t>(arenaCapacityFor(64 * 1024, 32 * 1024)));
  TEST_ASSERT_EQUAL_UINT32(32u * 1024u, static_cast<uint32_t>(arenaCapacityFor(0, 32 * 1024)));
  TEST_ASSERT_EQUAL_UINT32(32u * 1024u, static_cast<uint32_t>(arenaCapacityFor(-1, 32 * 1024)));
}

static void test_right_sized_arena_takes_the_whole_body() {
  BodyArena a;
  TEST_ASSERT_TRUE(a.init(arenaCapacityFor(7, 8192)));
  TEST_ASSERT_EQUAL_UINT32(7u, static_cast<uint32_t>(a.capacity()));
  a.open(8192);
  a.append("abcdefg", 7);
  a.finish();
  TEST_ASSERT_EQUAL_STRING("abcdefg", std::string(a.view()).c_str());
}

// An oversized upload declares its real length, so the arena is capped and the body overflows
// into the 413 rather than being truncated.
static void test_body_over_the_cap_still_overflows() {
  BodyArena a;
  TEST_ASSERT_TRUE(a.init(arenaCapacityFor(9, 4)));
  TEST_ASSERT_EQUAL_UINT32(4u, static_cast<uint32_t>(a.capacity()));
  a.open(4);
  a.append("123456789", 9);
  a.finish();
  TEST_ASSERT_EQUAL(static_cast<int>(BodyArena::State::Overflow), static_cast<int>(a.state()));
}

// A client that understates Content-Length must not write past the buffer it paid for.
static void test_understated_content_length_cannot_overrun() {
  BodyArena a;
  TEST_ASSERT_TRUE(a.init(arenaCapacityFor(4, 8192)));
  a.open(8192);
  a.append("1234", 4);
  a.append("5678", 4);
  a.finish();
  TEST_ASSERT_EQUAL(static_cast<int>(BodyArena::State::Overflow), static_cast<int>(a.state()));
}

int main(int, char**) {
  UNITY_BEGIN();
  RUN_TEST(test_boot_init_then_simple_body);
  RUN_TEST(test_chunked_append_reassembles);
  RUN_TEST(test_reuse_across_requests);
  RUN_TEST(test_route_cap_overflows_without_truncating);
  RUN_TEST(test_overflow_on_the_boundary_chunk);
  RUN_TEST(test_exactly_at_cap_is_fine);
  RUN_TEST(test_cap_over_capacity_is_clamped);
  RUN_TEST(test_reset_abandons_an_aborted_request);
  RUN_TEST(test_view_is_empty_unless_done);
  RUN_TEST(test_empty_body_is_done_and_empty);
  RUN_TEST(test_release_makes_arena_absent);
  RUN_TEST(test_release_on_absent_arena_is_a_noop);
  RUN_TEST(test_transient_lazy_init_release_cycle);
  RUN_TEST(test_content_length_sizes_the_allocation);
  RUN_TEST(test_right_sized_arena_takes_the_whole_body);
  RUN_TEST(test_body_over_the_cap_still_overflows);
  RUN_TEST(test_understated_content_length_cannot_overrun);
  return UNITY_END();
}
