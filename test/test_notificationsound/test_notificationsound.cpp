#include <unity.h>

#include <string>
#include <vector>

#include "core/sound/NotificationSound.h"

// awtrix::Source is the transport a command came in on. The one this file means is the sound
// source, and it stays spelled out so the two can never be read for each other.
using namespace awtrix;
using awtrix::sound::requestForSpec;

void setUp() {}
void tearDown() {}

namespace {

void assertSource(sound::Source expected, sound::Source actual, const char* what) {
  TEST_ASSERT_EQUAL_INT_MESSAGE(static_cast<int>(expected), static_cast<int>(actual), what);
}

void test_a_spec_without_sound_asks_for_nothing(void) {
  AppSpec spec;
  const sound::Request req = requestForSpec(spec);
  TEST_ASSERT_FALSE(req.present);
  TEST_ASSERT_TRUE(req.value.empty());
}

void test_a_name_is_left_open(void) {
  AppSpec spec;
  spec.sound = "ding";

  const sound::Request req = requestForSpec(spec);
  TEST_ASSERT_TRUE(req.present);
  assertSource(sound::Source::Auto, req.source, "a bare name");
  TEST_ASSERT_EQUAL_STRING("ding", req.value.c_str());
}

// AWTRIX 2 clients send the DFPlayer track number in the same field. It stays a name here: which
// sink answers is the router's decision, not the payload parser's.
void test_a_numeric_name_is_still_a_name(void) {
  AppSpec spec;
  spec.sound = "7";

  const sound::Request req = requestForSpec(spec);
  TEST_ASSERT_TRUE(req.present);
  assertSource(sound::Source::Auto, req.source, "a numeric name");
  TEST_ASSERT_EQUAL_STRING("7", req.value.c_str());
}

void test_spelled_out_rtttl_wins(void) {
  AppSpec spec;
  spec.sound = "ding";
  spec.extrasMut().rtttl = "beep:d=4,o=5,b=120:c";

  const sound::Request req = requestForSpec(spec);
  TEST_ASSERT_TRUE(req.present);
  assertSource(sound::Source::Rtttl, req.source, "soundRtttl beside sound");
  TEST_ASSERT_EQUAL_STRING("beep:d=4,o=5,b=120:c", req.value.c_str());
}

void test_rtttl_alone_is_enough(void) {
  AppSpec spec;
  spec.extrasMut().rtttl = "beep:d=4,o=5,b=120:c";

  const sound::Request req = requestForSpec(spec);
  TEST_ASSERT_TRUE(req.present);
  assertSource(sound::Source::Rtttl, req.source, "soundRtttl alone");
}

// The extras block exists but is empty - the copy-on-write allocation must not count as a request.
void test_empty_extras_do_not_count(void) {
  AppSpec spec;
  spec.extrasMut().progress = 50;

  TEST_ASSERT_FALSE(requestForSpec(spec).present);
}

}  // namespace

int main(int, char**) {
  UNITY_BEGIN();
  RUN_TEST(test_a_spec_without_sound_asks_for_nothing);
  RUN_TEST(test_a_name_is_left_open);
  RUN_TEST(test_a_numeric_name_is_still_a_name);
  RUN_TEST(test_spelled_out_rtttl_wins);
  RUN_TEST(test_rtttl_alone_is_enough);
  RUN_TEST(test_empty_extras_do_not_count);
  return UNITY_END();
}
