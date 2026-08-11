#include <unity.h>

#include "core/script/ScriptHeap.h"
#include "core/script/ScriptHeapTesting.h"

using namespace awtrix::script;

void setUp() {}
void tearDown() {}

static void test_growth_budget_is_unbounded_by_default() {
  heap::testing::resetGrowthBudget();
  TEST_ASSERT_TRUE(heap::growthBudget() > 1024u * 1024u);
}

static void test_growth_budget_honours_the_test_hook() {
  heap::testing::setGrowthBudget(512);
  TEST_ASSERT_EQUAL_UINT32(512u, static_cast<uint32_t>(heap::growthBudget()));
  heap::testing::resetGrowthBudget();
}

int main(int, char**) {
  UNITY_BEGIN();
  RUN_TEST(test_growth_budget_is_unbounded_by_default);
  RUN_TEST(test_growth_budget_honours_the_test_hook);
  return UNITY_END();
}
