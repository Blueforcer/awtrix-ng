#include <unity.h>
#include <cstdint>
#include <type_traits>
#include <utility>

#include "media/PodBuffer.h"

using Buffer = awtrix::media::PodBuffer<uint32_t>;
static_assert(!std::is_copy_constructible<Buffer>::value);
static_assert(!std::is_copy_assignable<Buffer>::value);
static_assert(std::is_nothrow_move_constructible<Buffer>::value);
static_assert(std::is_nothrow_move_assignable<Buffer>::value);

void setUp() {}
void tearDown() {}

static void assertReusable(Buffer& source) {
  TEST_ASSERT_NULL(source.data());
  TEST_ASSERT_EQUAL_UINT(0, source.size());
  TEST_ASSERT_TRUE(source.empty());
  TEST_ASSERT_TRUE(source.resize(1));
  TEST_ASSERT_NOT_NULL(source.data());
  source[0] = 99;
}

static void test_move_constructor_transfers_storage_and_resets_source() {
  Buffer source;
  TEST_ASSERT_TRUE(source.resize(64));
  source[0] = 42;
  const auto* data = source.data();
  Buffer target(std::move(source));
  TEST_ASSERT_EQUAL_PTR(data, target.data());
  TEST_ASSERT_EQUAL_UINT(64, target.size());
  assertReusable(source);
  TEST_ASSERT_EQUAL_UINT(42, target[0]);
}

static void test_move_assignment_replaces_storage_and_resets_source() {
  Buffer source, target;
  TEST_ASSERT_TRUE(source.resize(64));
  TEST_ASSERT_TRUE(target.resize(8));
  source[63] = 42;
  const auto* data = source.data();
  target = std::move(source);
  TEST_ASSERT_EQUAL_PTR(data, target.data());
  TEST_ASSERT_EQUAL_UINT(64, target.size());
  assertReusable(source);
  TEST_ASSERT_EQUAL_UINT(42, target[63]);
}

static void test_self_move_keeps_buffer_usable() {
  Buffer buffer;
  TEST_ASSERT_TRUE(buffer.resize(8));
  buffer[0] = 42;
  buffer = std::move(buffer);
  TEST_ASSERT_EQUAL_UINT(8, buffer.size());
  TEST_ASSERT_EQUAL_UINT(42, buffer[0]);
  TEST_ASSERT_TRUE(buffer.resize(64));
  TEST_ASSERT_EQUAL_UINT(42, buffer[0]);
}

static void test_moving_empty_buffer_releases_destination() {
  Buffer source, target;
  TEST_ASSERT_TRUE(target.resize(64));
  target = std::move(source);
  assertReusable(source);
  assertReusable(target);
}

int main(int, char**) {
  UNITY_BEGIN();
  RUN_TEST(test_move_constructor_transfers_storage_and_resets_source);
  RUN_TEST(test_move_assignment_replaces_storage_and_resets_source);
  RUN_TEST(test_self_move_keeps_buffer_usable);
  RUN_TEST(test_moving_empty_buffer_releases_destination);
  return UNITY_END();
}
