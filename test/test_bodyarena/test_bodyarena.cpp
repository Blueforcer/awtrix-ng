
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

// The buffer is exactly the declared length, so a 7-byte body costs 7 bytes however much room
// the device had to spare.
static void test_a_body_costs_its_own_length_and_no_more() {
  BodyArena a;
  TEST_ASSERT_TRUE(a.init(7));
  TEST_ASSERT_EQUAL_UINT32(7u, static_cast<uint32_t>(a.capacity()));
  a.open(8192);
  a.append("abcdefg", 7);
  a.finish();
  TEST_ASSERT_EQUAL_STRING("abcdefg", std::string(a.view()).c_str());
}

// A client that understates Content-Length must not write past the buffer it paid for.
static void test_understated_content_length_cannot_overrun() {
  BodyArena a;
  TEST_ASSERT_TRUE(a.init(4));
  a.open(8192);
  a.append("1234", 4);
  a.append("5678", 4);
  a.finish();
  TEST_ASSERT_EQUAL(static_cast<int>(BodyArena::State::Overflow), static_cast<int>(a.state()));
}

// openSourceArena is what HttpApiServer::collectBody calls at RAW_START for a raw script
// source; HttpApiServer.cpp itself is Arduino-only and cannot be unit tested, so this is the
// closest host-testable proxy for that seam.
static void test_open_source_arena_declared_length_fits() {
  BodyArena a;
  openSourceArena(a, 400, 1000);
  TEST_ASSERT_TRUE(a.ready());
  TEST_ASSERT_EQUAL_UINT32(400u, static_cast<uint32_t>(a.capacity()));
  TEST_ASSERT_EQUAL(static_cast<int>(BodyArena::State::Open), static_cast<int>(a.state()));
  const std::string body(400, 'x');
  a.append(body.data(), body.size());
  a.finish();
  TEST_ASSERT_EQUAL_UINT32(400u, static_cast<uint32_t>(a.view().size()));
}

// A declared length past the ceiling could never fit, so it is turned away without a byte being
// allocated for it.
static void test_open_source_arena_allocates_nothing_for_a_length_past_the_ceiling() {
  BodyArena past;
  openSourceArena(past, 5000, 1000);
  TEST_ASSERT_FALSE(past.ready());
  TEST_ASSERT_EQUAL_UINT32(0u, static_cast<uint32_t>(past.capacity()));
  TEST_ASSERT_EQUAL(static_cast<int>(BodyArena::State::Overflow), static_cast<int>(past.state()));
}

// No declared length is an empty body, not a body too large for the device: WebServer reads
// nothing for it either. Leaving it Idle rather than Overflow is what keeps the empty upload
// answered as the empty upload it is, instead of as a refusal quoting a size nobody sent.
static void test_open_source_arena_leaves_an_undeclared_length_idle() {
  BodyArena undeclared;
  openSourceArena(undeclared, 0, 8192);
  TEST_ASSERT_FALSE(undeclared.ready());
  TEST_ASSERT_EQUAL_UINT32(0u, static_cast<uint32_t>(undeclared.capacity()));
  TEST_ASSERT_EQUAL(static_cast<int>(BodyArena::State::Idle),
                    static_cast<int>(undeclared.state()));

  // Even on an arena still carrying a buffer from the request before it.
  BodyArena reused;
  openSourceArena(reused, 500, 8192);
  TEST_ASSERT_TRUE(reused.ready());
  openSourceArena(reused, 0, 8192);
  TEST_ASSERT_FALSE(reused.ready());
  TEST_ASSERT_EQUAL(static_cast<int>(BodyArena::State::Idle), static_cast<int>(reused.state()));
}

// "Never got a buffer" and "got one and overran it" both have to end in Overflow, because that
// single state is what takeBody() answers the upload on. The author is told the same thing
// either way: the difference is not one they can act on.
static void test_both_ways_of_not_fitting_end_in_overflow() {
  BodyArena refused;
  openSourceArena(refused, 5000, 1000);
  TEST_ASSERT_FALSE(refused.ready());
  TEST_ASSERT_EQUAL(static_cast<int>(BodyArena::State::Overflow),
                    static_cast<int>(refused.state()));

  BodyArena overran;
  openSourceArena(overran, 100, 1000);
  const std::string body(101, 'x');
  overran.append(body.data(), body.size());
  overran.finish();
  TEST_ASSERT_TRUE(overran.ready());
  TEST_ASSERT_EQUAL(static_cast<int>(BodyArena::State::Overflow),
                    static_cast<int>(overran.state()));
}

// The oversized-declared-length refusal must hold even when the arena still carries a buffer
// from an earlier request on the same connection, not just on a freshly constructed one.
static void test_open_source_arena_refuses_even_on_a_reused_arena() {
  BodyArena a;
  openSourceArena(a, 500, 1000);
  TEST_ASSERT_TRUE(a.ready());

  openSourceArena(a, 5000, 1000);
  TEST_ASSERT_FALSE(a.ready());
  TEST_ASSERT_EQUAL_UINT32(0u, static_cast<uint32_t>(a.capacity()));
  TEST_ASSERT_EQUAL(static_cast<int>(BodyArena::State::Overflow), static_cast<int>(a.state()));
}

// takeBody() reads "did the arena take charge of this body" off the arena's own state, and that
// is what picks the ceiling its refusal quotes. Every state a body the arena collected can leave
// behind has to choose the stored reading; only one it never held may be measured fresh.
// A stale stored reading winning over a fresh one is the bug this pins: a small upload would be
// refused against a ceiling belonging to some earlier request.
static void test_the_arena_state_decides_which_ceiling_is_quoted() {
  const std::size_t stored = 5000, live = 3000;

  BodyArena refused;
  openSourceArena(refused, 9000, stored);
  TEST_ASSERT_EQUAL_UINT32(
      static_cast<uint32_t>(stored),
      static_cast<uint32_t>(sourceCeilingFor(refused.state() != BodyArena::State::Idle, stored,
                                             live)));

  BodyArena overran;
  openSourceArena(overran, 100, stored);
  const std::string body(101, 'x');
  overran.append(body.data(), body.size());
  overran.finish();
  TEST_ASSERT_EQUAL_UINT32(
      static_cast<uint32_t>(stored),
      static_cast<uint32_t>(sourceCeilingFor(overran.state() != BodyArena::State::Idle, stored,
                                             live)));

  BodyArena untouched;
  TEST_ASSERT_EQUAL_UINT32(
      static_cast<uint32_t>(live),
      static_cast<uint32_t>(sourceCeilingFor(untouched.state() != BodyArena::State::Idle, stored,
                                             live)));
}

// Both refusals answer with the same sentence, so the figure is the only thing that varies and
// the reference has one row to describe.
static void test_a_refused_source_upload_names_the_room_there_was() {
  TEST_ASSERT_EQUAL_STRING("script source exceeds the 1234 bytes free to receive it",
                           sourceTooLargeMessage(1234).c_str());
  TEST_ASSERT_EQUAL_STRING("script source exceeds the 0 bytes free to receive it",
                           sourceTooLargeMessage(0).c_str());
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
  RUN_TEST(test_a_body_costs_its_own_length_and_no_more);
  RUN_TEST(test_understated_content_length_cannot_overrun);
  RUN_TEST(test_open_source_arena_declared_length_fits);
  RUN_TEST(test_open_source_arena_allocates_nothing_for_a_length_past_the_ceiling);
  RUN_TEST(test_open_source_arena_leaves_an_undeclared_length_idle);
  RUN_TEST(test_both_ways_of_not_fitting_end_in_overflow);
  RUN_TEST(test_open_source_arena_refuses_even_on_a_reused_arena);
  RUN_TEST(test_the_arena_state_decides_which_ceiling_is_quoted);
  RUN_TEST(test_a_refused_source_upload_names_the_room_there_was);
  return UNITY_END();
}
