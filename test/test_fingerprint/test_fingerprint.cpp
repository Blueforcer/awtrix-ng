#include <unity.h>

#include <cstddef>
#include <cstdint>

#include "core/render/Canvas.h"
#include "core/render/Fingerprint.h"

using namespace awtrix;

namespace {
uint32_t fnv1a(const uint8_t* bytes, std::size_t length) {
  uint32_t hash = 2166136261u;
  for (std::size_t i = 0; i < length; ++i) {
    hash ^= bytes[i];
    hash *= 16777619u;
  }
  return hash;
}

uint32_t ofCanvas(const Canvas& canvas) {
  Fingerprint print;
  print.add(canvas);
  return print.value();
}
}

void setUp() {}
void tearDown() {}

static void test_values_are_hashed_as_fnv1a_low_byte_first() {
  Fingerprint print;
  TEST_ASSERT_EQUAL_HEX32(2166136261u, print.value());
  print.add(0x04030201u);
  const uint8_t bytes[] = {1, 2, 3, 4};
  TEST_ASSERT_EQUAL_HEX32(fnv1a(bytes, sizeof(bytes)), print.value());
}

static void test_the_same_picture_gives_the_same_print_and_one_pixel_changes_it() {
  Canvas a(32, 8);
  Canvas b(32, 8);
  a.clear(0x102030);
  b.clear(0x102030);
  TEST_ASSERT_EQUAL_HEX32(ofCanvas(a), ofCanvas(b));
  b.setPixel(31, 7, 0x102031);
  TEST_ASSERT_NOT_EQUAL(ofCanvas(a), ofCanvas(b));
}

static void test_text_keeps_its_boundaries() {
  Fingerprint joined;
  joined.add("ab");
  joined.add("");
  Fingerprint split;
  split.add("a");
  split.add("b");
  TEST_ASSERT_NOT_EQUAL(joined.value(), split.value());
}

int main(int, char**) {
  UNITY_BEGIN();
  RUN_TEST(test_values_are_hashed_as_fnv1a_low_byte_first);
  RUN_TEST(test_the_same_picture_gives_the_same_print_and_one_pixel_changes_it);
  RUN_TEST(test_text_keeps_its_boundaries);
  return UNITY_END();
}
