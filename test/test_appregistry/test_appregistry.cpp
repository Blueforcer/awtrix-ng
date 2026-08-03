#include <unity.h>

#include "core/apps/AppRegistry.h"
#include "core/apps/builtin/DateApp.h"
#include "core/apps/builtin/TimeApp.h"

using namespace awtrix;

void setUp() {}
void tearDown() {}

static void test_register_and_find() {
  AppRegistry r;
  TimeApp t;
  DateApp d;
  r.add(&t);
  r.add(&d);
  TEST_ASSERT_EQUAL_UINT(2u, (unsigned)r.size());
  TEST_ASSERT_TRUE(r.find("Time") == &t);
  TEST_ASSERT_TRUE(r.find("Date") == &d);
  TEST_ASSERT_TRUE(r.find("Nope") == nullptr);
}

static void test_null_ignored() {
  AppRegistry r;
  r.add(nullptr);
  TEST_ASSERT_EQUAL_UINT(0u, (unsigned)r.size());
}

static void test_remove() {
  AppRegistry r;
  TimeApp t;
  DateApp d;
  r.add(&t);
  r.add(&d);
  r.remove("Time");
  TEST_ASSERT_EQUAL_UINT(1u, (unsigned)r.size());
  TEST_ASSERT_TRUE(r.find("Time") == nullptr);
  TEST_ASSERT_TRUE(r.find("Date") == &d);
}

static void test_remove_unknown_is_noop() {
  AppRegistry r;
  TimeApp t;
  r.add(&t);
  r.remove("Nope");
  TEST_ASSERT_EQUAL_UINT(1u, (unsigned)r.size());
  TEST_ASSERT_TRUE(r.find("Time") == &t);
}

static void test_re_add_replaces_mapping() {
  AppRegistry r;
  TimeApp a;
  TimeApp b;
  r.add(&a);
  r.add(&b);
  TEST_ASSERT_EQUAL_UINT(1u, (unsigned)r.size());
  TEST_ASSERT_TRUE(r.find("Time") == &b);
}

int main(int, char**) {
  UNITY_BEGIN();
  RUN_TEST(test_register_and_find);
  RUN_TEST(test_null_ignored);
  RUN_TEST(test_remove);
  RUN_TEST(test_remove_unknown_is_noop);
  RUN_TEST(test_re_add_replaces_mapping);
  return UNITY_END();
}
