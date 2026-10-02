#include <unity.h>

#include "core/api/ButtonEventJson.h"

using awtrix::api::buttonEventJson;

void setUp() {}
void tearDown() {}

static void test_body_matches_the_documented_shape() {
  TEST_ASSERT_EQUAL_STRING("{\"button\":\"left\",\"state\":true,\"uid\":\"dcda0c29dcb8\"}",
                           buttonEventJson("left", true, "dcda0c29dcb8").c_str());
  TEST_ASSERT_EQUAL_STRING("{\"button\":\"middle\",\"state\":false,\"uid\":\"\"}",
                           buttonEventJson("middle", false, "").c_str());
}

static void test_uid_is_escaped() {
  TEST_ASSERT_EQUAL_STRING("{\"button\":\"right\",\"state\":true,\"uid\":\"a\\\"b\"}",
                           buttonEventJson("right", true, "a\"b").c_str());
}

int main() {
  UNITY_BEGIN();
  RUN_TEST(test_body_matches_the_documented_shape);
  RUN_TEST(test_uid_is_escaped);
  return UNITY_END();
}
