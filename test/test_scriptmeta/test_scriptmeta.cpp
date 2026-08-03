
#include <unity.h>

#include "core/script/ScriptMeta.h"

using namespace awtrix;

void setUp() {}
void tearDown() {}

static void test_parse_header() {
  auto m = script::parseMeta(
      "# @name    Weather\n# @desc  Temp via Open-Meteo\n# @author blueray\n"
      "# @version 1.0\nimport json\n");
  TEST_ASSERT_EQUAL_STRING("Weather", m.name.c_str());
  TEST_ASSERT_EQUAL_STRING("Temp via Open-Meteo", m.desc.c_str());
  TEST_ASSERT_EQUAL_STRING("blueray", m.author.c_str());
  TEST_ASSERT_EQUAL_STRING("1.0", m.version.c_str());
}

static void test_missing_header_is_empty() {
  auto m = script::parseMeta("def draw() end");
  TEST_ASSERT_EQUAL_STRING("", m.name.c_str());
}

static void test_empty_source_is_empty() {
  auto m = script::parseMeta("");
  TEST_ASSERT_EQUAL_STRING("", m.name.c_str());
  TEST_ASSERT_EQUAL_STRING("", m.desc.c_str());
  TEST_ASSERT_EQUAL_STRING("", m.author.c_str());
  TEST_ASSERT_EQUAL_STRING("", m.version.c_str());
}

static void test_crlf_line_endings() {
  auto m = script::parseMeta("# @name Clock\r\n# @version 2.1\r\ndef draw() end\r\n");
  TEST_ASSERT_EQUAL_STRING("Clock", m.name.c_str());
  TEST_ASSERT_EQUAL_STRING("2.1", m.version.c_str());
}

static void test_blank_lines_and_plain_comments_do_not_stop_the_scan() {
  auto m = script::parseMeta(
      "\n"
      "# Weather app for AWTRIX NG\n"
      "\n"
      "# @name Weather\n"
      "#\n"
      "# @author blueray\n"
      "def draw() end\n");
  TEST_ASSERT_EQUAL_STRING("Weather", m.name.c_str());
  TEST_ASSERT_EQUAL_STRING("blueray", m.author.c_str());
}

static void test_scan_stops_at_first_code_line() {
  auto m = script::parseMeta(
      "# @name Real\n"
      "def draw()\n"
      "  # @name Sneaky\n"
      "end\n");
  TEST_ASSERT_EQUAL_STRING("Real", m.name.c_str());
}

static void test_unknown_keys_and_bare_keys_are_ignored() {
  auto m = script::parseMeta(
      "# @license MIT\n"
      "# @name\n"
      "# @desc Hello\n"
      "def draw() end\n");
  TEST_ASSERT_EQUAL_STRING("", m.name.c_str());
  TEST_ASSERT_EQUAL_STRING("Hello", m.desc.c_str());
}

static void test_lenient_formatting() {
  auto m = script::parseMeta(
      "  #@name Indented\n"
      "#   @Author  Someone  \n"
      "def draw() end\n");
  TEST_ASSERT_EQUAL_STRING("Indented", m.name.c_str());
  TEST_ASSERT_EQUAL_STRING("Someone", m.author.c_str());
}

static void test_header_only_without_trailing_newline() {
  auto m = script::parseMeta("# @name Solo");
  TEST_ASSERT_EQUAL_STRING("Solo", m.name.c_str());
}

static void test_bare_module_flag_takes_the_file_name() {
  auto m = script::parseMeta("# @module\ndef draw() end\n");
  TEST_ASSERT_TRUE(m.module);
  TEST_ASSERT_EQUAL_STRING("", m.moduleName.c_str());
}

static void test_module_can_name_its_import() {
  auto m = script::parseMeta("# @module weather\n# @desc Helpers\ndef draw() end\n");
  TEST_ASSERT_TRUE(m.module);
  TEST_ASSERT_EQUAL_STRING("weather", m.moduleName.c_str());
  TEST_ASSERT_EQUAL_STRING("Helpers", m.desc.c_str());
}

static void test_without_the_directive_a_script_is_not_a_module() {
  auto m = script::parseMeta("# @name Clock\ndef draw() end\n");
  TEST_ASSERT_FALSE(m.module);
}

static void test_config_lines_are_flagged_but_not_read() {
  auto m = script::parseMeta(
      "# @name Weather\n"
      "# @config city text \"City\" default=\"Berlin\"\n"
      "# @author blueray\n"
      "def draw() end\n");
  TEST_ASSERT_TRUE(m.hasConfig);
  TEST_ASSERT_EQUAL_STRING("Weather", m.name.c_str());
  TEST_ASSERT_EQUAL_STRING("blueray", m.author.c_str());
}

static void test_a_bare_config_line_still_counts() {
  TEST_ASSERT_TRUE(script::parseMeta("# @config\ndef draw() end\n").hasConfig);
  TEST_ASSERT_FALSE(script::parseMeta("# @name Clock\ndef draw() end\n").hasConfig);
}

int main(int, char**) {
  UNITY_BEGIN();
  RUN_TEST(test_parse_header);
  RUN_TEST(test_missing_header_is_empty);
  RUN_TEST(test_empty_source_is_empty);
  RUN_TEST(test_crlf_line_endings);
  RUN_TEST(test_blank_lines_and_plain_comments_do_not_stop_the_scan);
  RUN_TEST(test_scan_stops_at_first_code_line);
  RUN_TEST(test_unknown_keys_and_bare_keys_are_ignored);
  RUN_TEST(test_lenient_formatting);
  RUN_TEST(test_header_only_without_trailing_newline);
  RUN_TEST(test_bare_module_flag_takes_the_file_name);
  RUN_TEST(test_module_can_name_its_import);
  RUN_TEST(test_without_the_directive_a_script_is_not_a_module);
  RUN_TEST(test_config_lines_are_flagged_but_not_read);
  RUN_TEST(test_a_bare_config_line_still_counts);
  return UNITY_END();
}
