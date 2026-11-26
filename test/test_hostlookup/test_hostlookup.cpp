#include <unity.h>

#include <memory>
#include <string>

#include "transport/net/HostLookup.cpp"

using namespace awtrix;
using net::HostLookup;
using net::ResolveState;
using Result = HostLookup::Result;

namespace {

struct Resolver {
  ResolveState answer = ResolveState::Pending;
  uint32_t address = 0;
  int made = 0;
  int resolves = 0;
  int forgets = 0;
  std::string asked;
} resolver;

class FakeResolver final : public net::IHostResolver {
 public:
  ResolveState resolve(const std::string& host) override {
    ++resolver.resolves;
    resolver.asked = host;
    return resolver.answer;
  }
  uint32_t address() const override { return resolver.address; }
  net::LinkError error() const override { return net::LinkError::None; }
  void forget() override { ++resolver.forgets; }
};

constexpr uint32_t kFirst = 0x0A000001u;
constexpr uint32_t kSecond = 0x0A000002u;

void found(HostLookup& lookup, int64_t nowMs, uint32_t address) {
  resolver.answer = ResolveState::Ready;
  resolver.address = address;
  TEST_ASSERT_EQUAL(Result::Found, lookup.poll(nowMs, false, false));
}

}

namespace awtrix {
namespace net {
std::unique_ptr<IHostResolver> makeHostResolver() {
  ++resolver.made;
  return std::unique_ptr<IHostResolver>(new FakeResolver());
}
}
}

void setUp() { resolver = Resolver{}; }
void tearDown() {}

static void test_nothing_is_looked_up_without_a_name() {
  HostLookup lookup;
  TEST_ASSERT_EQUAL(Result::Idle, lookup.poll(0, false, false));
  TEST_ASSERT_EQUAL_INT(0, resolver.made);
}

static void test_a_name_is_followed_until_it_is_found() {
  HostLookup lookup;
  lookup.setHost("clock");
  TEST_ASSERT_EQUAL(Result::Pending, lookup.poll(0, false, false));
  TEST_ASSERT_EQUAL(Result::Pending, lookup.poll(100, false, false));
  TEST_ASSERT_EQUAL_STRING("clock", resolver.asked.c_str());
  found(lookup, 200, kFirst);
  TEST_ASSERT_EQUAL_HEX32(kFirst, lookup.address());
  TEST_ASSERT_EQUAL_INT(1, resolver.made);
}

static void test_a_failed_lookup_is_tried_again_after_ten_seconds() {
  HostLookup lookup;
  lookup.setHost("clock");
  resolver.answer = ResolveState::Failed;
  TEST_ASSERT_EQUAL(Result::Failed, lookup.poll(1000, false, false));
  TEST_ASSERT_EQUAL_INT(1, resolver.forgets);
  TEST_ASSERT_EQUAL(Result::Idle, lookup.poll(1000 + HostLookup::kRetryMs - 1, false, false));
  TEST_ASSERT_EQUAL_INT(1, resolver.resolves);
  TEST_ASSERT_EQUAL(Result::Failed, lookup.poll(1000 + HostLookup::kRetryMs, false, false));
  TEST_ASSERT_EQUAL_INT(2, resolver.resolves);
}

static void test_a_known_host_that_answers_is_not_looked_up_again() {
  HostLookup lookup;
  lookup.setHost("clock");
  found(lookup, 0, kFirst);
  TEST_ASSERT_EQUAL(Result::Idle, lookup.poll(10 * HostLookup::kRelookMs, true, true));
  TEST_ASSERT_EQUAL_INT(1, resolver.resolves);
}

static void test_a_silent_host_is_looked_up_again_after_half_a_minute() {
  HostLookup lookup;
  lookup.setHost("clock");
  found(lookup, 1000, kFirst);
  TEST_ASSERT_EQUAL(Result::Idle, lookup.poll(1000 + HostLookup::kRelookMs - 1, true, false));
  TEST_ASSERT_EQUAL_INT(0, resolver.forgets);
  resolver.answer = ResolveState::Pending;
  TEST_ASSERT_EQUAL(Result::Pending, lookup.poll(1000 + HostLookup::kRelookMs, true, false));
  TEST_ASSERT_EQUAL_INT_MESSAGE(1, resolver.forgets, "the cached address is dropped before the new lookup");
  resolver.answer = ResolveState::Ready;
  resolver.address = kSecond;
  TEST_ASSERT_EQUAL(Result::Found, lookup.poll(1000 + HostLookup::kRelookMs + 50, true, false));
  TEST_ASSERT_EQUAL_HEX32(kSecond, lookup.address());
}

static void test_a_lookup_in_flight_goes_on_until_forgotten() {
  HostLookup lookup;
  lookup.setHost("clock");
  found(lookup, 0, kFirst);
  resolver.answer = ResolveState::Pending;
  TEST_ASSERT_EQUAL(Result::Pending, lookup.poll(HostLookup::kRelookMs, true, false));
  TEST_ASSERT_EQUAL(Result::Pending, lookup.poll(HostLookup::kRelookMs + 1, true, true));
  const int forgets = resolver.forgets;
  lookup.forget();
  TEST_ASSERT_EQUAL_INT(forgets + 1, resolver.forgets);
  TEST_ASSERT_EQUAL(Result::Idle, lookup.poll(HostLookup::kRelookMs + 2, true, true));
}

static void test_an_unknown_host_is_looked_up_at_once() {
  HostLookup lookup;
  lookup.setHost("clock");
  found(lookup, 0, kFirst);
  TEST_ASSERT_EQUAL(Result::Found, lookup.poll(1, false, false));
  TEST_ASSERT_EQUAL_INT(2, resolver.resolves);
}

static void test_a_new_name_starts_over() {
  HostLookup lookup;
  lookup.setHost("clock");
  resolver.answer = ResolveState::Failed;
  TEST_ASSERT_EQUAL(Result::Failed, lookup.poll(0, false, false));
  const int forgets = resolver.forgets;
  lookup.setHost("other");
  TEST_ASSERT_EQUAL_STRING("other", lookup.host().c_str());
  TEST_ASSERT_EQUAL_INT(forgets + 1, resolver.forgets);
  resolver.answer = ResolveState::Pending;
  TEST_ASSERT_EQUAL_MESSAGE(Result::Pending, lookup.poll(1, false, false), "no wait left over from the old name");
  TEST_ASSERT_EQUAL_STRING("other", resolver.asked.c_str());
}

int main(int, char**) {
  UNITY_BEGIN();
  RUN_TEST(test_nothing_is_looked_up_without_a_name);
  RUN_TEST(test_a_name_is_followed_until_it_is_found);
  RUN_TEST(test_a_failed_lookup_is_tried_again_after_ten_seconds);
  RUN_TEST(test_a_known_host_that_answers_is_not_looked_up_again);
  RUN_TEST(test_a_silent_host_is_looked_up_again_after_half_a_minute);
  RUN_TEST(test_a_lookup_in_flight_goes_on_until_forgotten);
  RUN_TEST(test_an_unknown_host_is_looked_up_at_once);
  RUN_TEST(test_a_new_name_starts_over);
  return UNITY_END();
}
