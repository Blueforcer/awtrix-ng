#include <unity.h>
#include <cstdlib>
#include "core/memory/CheckedShared.h"

using namespace awtrix::checked;
namespace {
int live = 0, allocations = 0, releases = 0;
bool fail = false;
struct Owner { Owner() { ++live; } ~Owner() { --live; } int value = 42; };
void* allocate(std::size_t bytes) { if (fail) return nullptr; ++allocations; return std::malloc(bytes); }
void release(void* block) { ++releases; std::free(block); }
}
void setUp() { live = allocations = releases = 0; fail = false; storage::setAllocator({allocate, release}); }
void tearDown() { storage::setAllocator({}); TEST_ASSERT_EQUAL_INT(0, live); }

static void test_failed_control_block_adoption_destroys_unpublished_owner() {
  auto owner = std::make_unique<Owner>();
  fail = true;
  auto shared = tryAdoptShared(std::move(owner));
  TEST_ASSERT_FALSE(shared);
  TEST_ASSERT_EQUAL_INT(0, live);
  TEST_ASSERT_EQUAL_INT(0, allocations);
}
static void test_failed_combined_allocation_does_not_construct_owner() {
  fail = true;
  TEST_ASSERT_FALSE(tryMakeShared<Owner>());
  TEST_ASSERT_EQUAL_INT(0, live);
}
static void test_alias_and_weak_owner_release_captured_allocator_exactly_once() {
  auto owner = tryMakeShared<Owner>();
  TEST_ASSERT_TRUE(owner);
  TEST_ASSERT_EQUAL_INT(1, allocations);
  std::shared_ptr<int> alias(owner, &owner->value);
  std::weak_ptr<Owner> weak(owner);
  storage::setAllocator({});
  owner.reset();
  TEST_ASSERT_EQUAL_INT(42, *alias);
  alias.reset();
  TEST_ASSERT_EQUAL_INT(0, live);
  TEST_ASSERT_EQUAL_INT(0, releases);
  weak.reset();
  TEST_ASSERT_EQUAL_INT(1, releases);
}
static void test_adopted_owner_and_control_release_after_last_copy() {
  auto owner = tryAdoptShared(std::make_unique<Owner>());
  auto copy = owner;
  owner.reset();
  TEST_ASSERT_EQUAL_INT(1, live);
  copy.reset();
  TEST_ASSERT_EQUAL_INT(1, allocations);
  TEST_ASSERT_EQUAL_INT(1, releases);
}
int main(int, char**) {
  UNITY_BEGIN();
  RUN_TEST(test_failed_control_block_adoption_destroys_unpublished_owner);
  RUN_TEST(test_failed_combined_allocation_does_not_construct_owner);
  RUN_TEST(test_alias_and_weak_owner_release_captured_allocator_exactly_once);
  RUN_TEST(test_adopted_owner_and_control_release_after_last_copy);
  return UNITY_END();
}
