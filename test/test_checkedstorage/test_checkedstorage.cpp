#include <unity.h>

#include <cstdlib>
#include <limits>
#include <string_view>

#include "core/memory/CheckedStorage.h"

using namespace awtrix::checked;

namespace {
int remaining = -1;
int blocks = 0;
int allocations = 0;
int originalReleases = 0;
int replacementReleases = 0;
void* acquire(std::size_t bytes) {
  ++allocations;
  if (remaining == 0) return nullptr;
  if (remaining > 0) --remaining;
  void* p = std::malloc(bytes);
  if (p) ++blocks;
  return p;
}
void release(void* p) { ++originalReleases; --blocks; std::free(p); }
void otherRelease(void* p) { ++replacementReleases; --blocks; std::free(p); }
constexpr const char* longText = "a prepared layout string longer than the inline buffer";
struct Entry {
  CheckedString text;
  bool valid() const { return text.valid(); }
};
}

void setUp() {
  remaining = -1;
  blocks = allocations = originalReleases = replacementReleases = 0;
  storage::setAllocator({acquire, release});
}
void tearDown() {
  TEST_ASSERT_EQUAL_INT(0, blocks);
  storage::setAllocator({});
}

static void test_string_copy_failure_is_visible_and_preserves_previous_bytes() {
  CheckedString original(longText);
  CheckedString target("previous");
  remaining = 0;
  target = original;
  TEST_ASSERT_FALSE(target.valid());
  TEST_ASSERT_EQUAL_STRING("previous", target.c_str());
  TEST_ASSERT_TRUE(original.valid());
  CheckedString failedCopy(original);
  TEST_ASSERT_FALSE(failedCopy.valid());
  TEST_ASSERT_TRUE(failedCopy.empty());
  CheckedString copiedFailure(failedCopy);
  TEST_ASSERT_FALSE(copiedFailure.valid());
  CheckedString assignedFailure("previous");
  TEST_ASSERT_FALSE(assignedFailure.assign(failedCopy));
  TEST_ASSERT_FALSE(assignedFailure.valid());
  TEST_ASSERT_TRUE(target.assign("reset"));
  TEST_ASSERT_TRUE(target.valid());
  TEST_ASSERT_EQUAL_STRING("reset", target.c_str());
}

static void test_string_move_and_self_view_assignment_do_not_allocate() {
  CheckedString original(longText);
  const int before = allocations;
  remaining = 0;
  CheckedString moved(std::move(original));
  TEST_ASSERT_TRUE(moved.valid());
  TEST_ASSERT_TRUE(original.valid());
  TEST_ASSERT_TRUE(original.empty());
  TEST_ASSERT_TRUE(moved.assign(moved.view().substr(2, 13)));
  TEST_ASSERT_EQUAL_STRING("prepared layo", moved.c_str());
  CheckedString destination("small");
  destination = std::move(moved);
  TEST_ASSERT_TRUE(destination.valid());
  TEST_ASSERT_TRUE(moved.empty());
  TEST_ASSERT_EQUAL_INT(before, allocations);
}

static void test_capacity_arithmetic_rejects_before_calling_allocator() {
  CheckedString text("safe");
  CheckedArray<int> numbers;
  const int before = allocations;
  TEST_ASSERT_FALSE(text.reserve(std::numeric_limits<std::size_t>::max()));
  TEST_ASSERT_FALSE(numbers.reserve(std::numeric_limits<std::size_t>::max()));
  TEST_ASSERT_EQUAL_INT(before, allocations);
  TEST_ASSERT_EQUAL_STRING("safe", text.c_str());
  TEST_ASSERT_FALSE(text.valid());
  TEST_ASSERT_FALSE(numbers.valid());
}

static void test_array_push_copy_and_growth_preserve_referenced_element() {
  CheckedArray<CheckedString> values;
  TEST_ASSERT_TRUE(values.emplace_back(longText));
  TEST_ASSERT_TRUE(values.push_back(values.front()));
  TEST_ASSERT_EQUAL_UINT(2, values.size());
  TEST_ASSERT_EQUAL_STRING(longText, values[0].c_str());
  TEST_ASSERT_EQUAL_STRING(longText, values[1].c_str());
  TEST_ASSERT_TRUE(values.resize(4, values[0]));
  for (const auto& value : values) TEST_ASSERT_EQUAL_STRING(longText, value.c_str());
  const int before = allocations;
  remaining = 0;
  CheckedArray<CheckedString> moved(std::move(values));
  TEST_ASSERT_TRUE(moved.valid());
  TEST_ASSERT_EQUAL_UINT(4, moved.size());
  TEST_ASSERT_TRUE(values.empty());
  TEST_ASSERT_EQUAL_INT(before, allocations);
}

static void test_each_nested_copy_allocation_failure_releases_partial_copy() {
  CheckedArray<Entry> source;
  Entry first; first.text = longText;
  TEST_ASSERT_TRUE(source.push_back(first));
  TEST_ASSERT_TRUE(source.push_back(first));
  const int baseline = blocks;
  for (int failure = 0; failure <= 3; ++failure) {
    {
      CheckedArray<Entry> target;
      remaining = failure;
      target = source;
      TEST_ASSERT_EQUAL_INT(failure == 3, target.valid());
      TEST_ASSERT_EQUAL_UINT(failure == 3 ? 2 : 0, target.size());
    }
    TEST_ASSERT_EQUAL_INT(baseline, blocks);
  }
  remaining = -1;
  CheckedArray<Entry> target;
  Entry previous; previous.text = "old";
  TEST_ASSERT_TRUE(target.push_back(previous));
  remaining = 1; // array succeeds, first nested string fails
  target = source;
  TEST_ASSERT_FALSE(target.valid());
  TEST_ASSERT_EQUAL_UINT(1, target.size());
  TEST_ASSERT_EQUAL_STRING("old", target[0].text.c_str());
  target.clear();
  TEST_ASSERT_TRUE(target.valid());
}

static void test_nested_invalid_value_cannot_be_admitted() {
  Entry invalid;
  remaining = 0;
  invalid.text = longText;
  TEST_ASSERT_FALSE(invalid.valid());
  remaining = -1;
  CheckedArray<Entry> values;
  TEST_ASSERT_FALSE(values.push_back(invalid));
  TEST_ASSERT_FALSE(values.valid());
  TEST_ASSERT_TRUE(values.empty());
  values.clear();
  Entry valid; valid.text = "valid";
  TEST_ASSERT_TRUE(values.push_back(valid));
  remaining = 0;
  values[0].text = longText;
  TEST_ASSERT_FALSE(values.valid());
}

static void test_buffers_release_through_the_allocator_that_created_them() {
  {
    CheckedString text(longText);
    CheckedArray<int> values{1, 2, 3};
    TEST_ASSERT_TRUE(values.valid());
    storage::setAllocator({acquire, otherRelease});
  }
  TEST_ASSERT_EQUAL_INT(2, originalReleases);
  TEST_ASSERT_EQUAL_INT(0, replacementReleases);
}

int main(int, char**) {
  UNITY_BEGIN();
  RUN_TEST(test_string_copy_failure_is_visible_and_preserves_previous_bytes);
  RUN_TEST(test_string_move_and_self_view_assignment_do_not_allocate);
  RUN_TEST(test_capacity_arithmetic_rejects_before_calling_allocator);
  RUN_TEST(test_array_push_copy_and_growth_preserve_referenced_element);
  RUN_TEST(test_each_nested_copy_allocation_failure_releases_partial_copy);
  RUN_TEST(test_nested_invalid_value_cannot_be_admitted);
  RUN_TEST(test_buffers_release_through_the_allocator_that_created_them);
  return UNITY_END();
}
