
#include <unity.h>

#include "core/script/ScriptError.h"

using namespace awtrix;

void setUp() {}
void tearDown() {}

static void test_compile_error_yields_line_and_clean_message() {
  auto e = script::parseScriptError("syntax_error: script:12: unexpected token ')'");
  TEST_ASSERT_EQUAL_INT(12, e.line);
  TEST_ASSERT_EQUAL_STRING("syntax_error: unexpected token ')'", e.message.c_str());
  TEST_ASSERT_EQUAL_STRING("", e.hook.c_str());
}

static void test_runtime_error_without_position_keeps_hook() {
  auto e = script::parseScriptError("runtime_error: operand must be number", "setup");
  TEST_ASSERT_EQUAL_INT(0, e.line);
  TEST_ASSERT_EQUAL_STRING("runtime_error: operand must be number", e.message.c_str());
  TEST_ASSERT_EQUAL_STRING("setup", e.hook.c_str());
}

static void test_runtime_error_with_position_reports_both() {
  auto e = script::parseScriptError("runtime_error: script:7: division by zero", "loop");
  TEST_ASSERT_EQUAL_INT(7, e.line);
  TEST_ASSERT_EQUAL_STRING("runtime_error: division by zero", e.message.c_str());
  TEST_ASSERT_EQUAL_STRING("loop", e.hook.c_str());
}

static void test_plain_message_passes_through() {
  auto e = script::parseScriptError("no draw() method");
  TEST_ASSERT_EQUAL_INT(0, e.line);
  TEST_ASSERT_EQUAL_STRING("no draw() method", e.message.c_str());
}

static void test_position_at_start_is_removed_cleanly() {
  auto e = script::parseScriptError("script:1: bad token");
  TEST_ASSERT_EQUAL_INT(1, e.line);
  TEST_ASSERT_EQUAL_STRING("bad token", e.message.c_str());
}

static void test_prose_mentioning_script_is_not_a_position() {
  auto e = script::parseScriptError("script: something went wrong");
  TEST_ASSERT_EQUAL_INT(0, e.line);
  TEST_ASSERT_EQUAL_STRING("script: something went wrong", e.message.c_str());

  auto e2 = script::parseScriptError("the script:abc: is odd");
  TEST_ASSERT_EQUAL_INT(0, e2.line);
  TEST_ASSERT_EQUAL_STRING("the script:abc: is odd", e2.message.c_str());
}

static void test_scan_continues_past_a_near_miss() {
  auto e = script::parseScriptError("script:12 no colon, but script:34: here");
  TEST_ASSERT_EQUAL_INT(34, e.line);
  TEST_ASSERT_EQUAL_STRING("script:12 no colon, but here", e.message.c_str());
}

static void test_implausible_line_number_is_rejected() {
  auto e = script::parseScriptError("script:99999999: nonsense");
  TEST_ASSERT_EQUAL_INT(0, e.line);
  TEST_ASSERT_EQUAL_STRING("script:99999999: nonsense", e.message.c_str());
}

static void test_zero_line_is_rejected() {
  auto e = script::parseScriptError("script:0: impossible");
  TEST_ASSERT_EQUAL_INT(0, e.line);
  TEST_ASSERT_EQUAL_STRING("script:0: impossible", e.message.c_str());
}

static void test_empty_inputs() {
  auto e = script::parseScriptError("", "");
  TEST_ASSERT_TRUE(e.empty());
  TEST_ASSERT_EQUAL_STRING("", e.hook.c_str());
  TEST_ASSERT_EQUAL_INT(0, e.line);
}

static void test_clear_resets_every_field() {
  auto e = script::parseScriptError("script:5: boom", "draw");
  e.clear();
  TEST_ASSERT_TRUE(e.empty());
  TEST_ASSERT_EQUAL_INT(0, e.line);
  TEST_ASSERT_EQUAL_STRING("", e.hook.c_str());
}

int main(int, char**) {
  UNITY_BEGIN();
  RUN_TEST(test_compile_error_yields_line_and_clean_message);
  RUN_TEST(test_runtime_error_without_position_keeps_hook);
  RUN_TEST(test_runtime_error_with_position_reports_both);
  RUN_TEST(test_plain_message_passes_through);
  RUN_TEST(test_position_at_start_is_removed_cleanly);
  RUN_TEST(test_prose_mentioning_script_is_not_a_position);
  RUN_TEST(test_scan_continues_past_a_near_miss);
  RUN_TEST(test_implausible_line_number_is_rejected);
  RUN_TEST(test_zero_line_is_rejected);
  RUN_TEST(test_empty_inputs);
  RUN_TEST(test_clear_resets_every_field);
  return UNITY_END();
}
