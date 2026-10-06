
#include <unity.h>

#include <string>

#include "core/script/ScriptMeta.h"

#include <fstream>
#include <sstream>
#include <vector>

#include "core/api/JsonReader.h"

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

static void test_ondemand_is_a_bare_flag_in_any_case() {
  TEST_ASSERT_TRUE(script::parseMeta("# @ondemand\ndef draw() end\n").onDemand);
  TEST_ASSERT_TRUE(script::parseMeta("# @name Doom\n# @onDemand\n").onDemand);
  TEST_ASSERT_FALSE(script::parseMeta("# @name Clock\n").onDemand);
}

static void test_modules_and_headless_scripts_are_never_ondemand() {
  TEST_ASSERT_FALSE(script::parseMeta("# @module\n# @ondemand\n").onDemand);
  TEST_ASSERT_FALSE(script::parseMeta("# @headless true\n# @ondemand\n").onDemand);
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

static void test_icons_one_line() {
  auto m = script::parseMeta("# @name Weather\n# @icons 2105, 2106 2107\ndef draw() end\n");
  auto ids = script::splitIcons(m.icons);
  TEST_ASSERT_EQUAL_INT(3, (int)ids.size());
  TEST_ASSERT_EQUAL_STRING("2105", ids[0].c_str());
  TEST_ASSERT_EQUAL_STRING("2106", ids[1].c_str());
  TEST_ASSERT_EQUAL_STRING("2107", ids[2].c_str());
}

static void test_several_icons_lines_are_one_list() {
  auto m = script::parseMeta("# @icons 1\n# @desc Between\n# @icons 2,3\ndef draw() end\n");
  auto ids = script::splitIcons(m.icons);
  TEST_ASSERT_EQUAL_INT(3, (int)ids.size());
  TEST_ASSERT_EQUAL_STRING("3", ids[2].c_str());
  TEST_ASSERT_EQUAL_STRING("Between", m.desc.c_str());
}

static void test_icons_without_a_value_is_empty() {
  TEST_ASSERT_EQUAL_STRING("", script::parseMeta("# @icons\ndef draw() end\n").icons.c_str());
  TEST_ASSERT_TRUE(script::splitIcons("").empty());
}

static void test_no_icons_line_is_an_empty_list() {
  auto m = script::parseMeta("# @name Clock\ndef draw() end\n");
  TEST_ASSERT_EQUAL_STRING("", m.icons.c_str());
  TEST_ASSERT_TRUE(script::splitIcons(m.icons).empty());
}

static void test_duplicate_ids_appear_once() {
  auto ids = script::splitIcons("7 7, 7  8");
  TEST_ASSERT_EQUAL_INT(2, (int)ids.size());
  TEST_ASSERT_EQUAL_STRING("8", ids[1].c_str());
}

static void test_unusable_entries_are_dropped() {
  auto ids = script::splitIcons("ok-1 ../etc a/b bad.gif ok_2");
  TEST_ASSERT_EQUAL_INT(2, (int)ids.size());
  TEST_ASSERT_EQUAL_STRING("ok-1", ids[0].c_str());
  TEST_ASSERT_EQUAL_STRING("ok_2", ids[1].c_str());
}

static void test_an_overlong_id_is_dropped() {
  const std::string just(script::kIconIdMax, 'a');
  const std::string over(script::kIconIdMax + 1, 'b');
  auto ids = script::splitIcons(just + " " + over);
  TEST_ASSERT_EQUAL_INT(1, (int)ids.size());
  TEST_ASSERT_EQUAL_STRING(just.c_str(), ids[0].c_str());
}

static void test_the_stored_text_is_bounded() {
  std::string header;
  for (int i = 0; i < 500; ++i) header += "# @icons " + std::string(script::kIconIdMax, 'a') + "\n";
  auto m = script::parseMeta(header + "def draw() end\n");
  TEST_ASSERT_TRUE(m.icons.size() <= script::kIconsMax * (script::kIconIdMax + 1));
}

static void test_the_list_is_capped() {
  std::string raw;
  for (std::size_t i = 0; i < script::kIconsMax + 10; ++i) raw += std::to_string(i) + " ";
  TEST_ASSERT_EQUAL_INT((int)script::kIconsMax, (int)script::splitIcons(raw).size());
}

static void test_requires_lines_list_names_and_hub_ids() {
  auto m = script::parseMeta("# @name Snake\n# @requires pad\n# @REQUIRES Gamepad AbC123xyz456 more\ndef draw() end\n");
  auto r = script::splitRequires(m.requirements);
  TEST_ASSERT_EQUAL_INT(2, (int)r.size());
  TEST_ASSERT_EQUAL_STRING("pad", r[0].name.c_str());
  TEST_ASSERT_EQUAL_STRING("", r[0].hub.c_str());
  TEST_ASSERT_EQUAL_STRING("Gamepad", r[1].name.c_str());
  TEST_ASSERT_EQUAL_STRING("AbC123xyz456", r[1].hub.c_str());
}

static void test_unusable_requires_lines_are_dropped_whole() {
  auto m = script::parseMeta(
      "# @requires\n# @requires pa.d\n# @requires pad short\n# @requires pad AbC123xyz45-\n"
      "# @requires " + std::string(33, 'a') + "\n# @requires pad\n# @requires pad AbC123xyz456\n"
      "def draw() end\n# @requires late\n");
  auto r = script::splitRequires(m.requirements);
  TEST_ASSERT_EQUAL_INT(1, (int)r.size());
  TEST_ASSERT_EQUAL_STRING("pad", r[0].name.c_str());
  TEST_ASSERT_EQUAL_STRING("", r[0].hub.c_str());
}

static void test_requires_are_capped() {
  std::string header;
  for (int i = 0; i < 12; ++i) header += "# @requires dep" + std::to_string(i) + "\n";
  auto r = script::splitRequires(script::parseMeta(header + "def draw() end\n").requirements);
  TEST_ASSERT_EQUAL_INT((int)script::kRequiresMax, (int)r.size());
  TEST_ASSERT_EQUAL_STRING("dep7", r.back().name.c_str());
}

// The same header vectors run through the web UI and the AWTRIX Hub parsers. The native test
// binary runs from the project directory, so the fixture is read by its repository path.
static std::string readHeaderVectors() {
  std::ifstream in("test/fixtures/script_header_vectors.json", std::ios::binary);
  std::stringstream text;
  text << in.rdbuf();
  return text.str();
}

static std::vector<std::string> vectorStrings(api::JsonReader list) {
  std::vector<std::string> out;
  if (!list.enterArray()) return out;
  while (list.nextElement()) {
    std::string value;
    list.appendString(value);
    out.push_back(value);
    list.skipValue();
  }
  return out;
}

static void test_header_matches_the_shared_vectors() {
  const std::string text = readHeaderVectors();
  TEST_ASSERT_FALSE_MESSAGE(text.empty(), "test/fixtures/script_header_vectors.json is not readable");
  api::JsonReader cases = api::memberValue(api::JsonReader(text), "cases");
  TEST_ASSERT_TRUE(cases.enterArray());
  int count = 0;
  while (cases.nextElement()) {
    const api::JsonReader item = cases;
    std::string name, source;
    TEST_ASSERT_TRUE(api::memberValue(item, "name").appendString(name));
    TEST_ASSERT_TRUE(api::memberValue(item, "source").appendString(source));
    const script::ScriptMeta meta = script::parseMeta(source);

    TEST_ASSERT_TRUE_MESSAGE(script::splitNeeds(meta.needs) == vectorStrings(api::memberValue(item, "needs")), name.c_str());
    TEST_ASSERT_TRUE_MESSAGE(script::splitIcons(meta.icons) == vectorStrings(api::memberValue(item, "icons")), name.c_str());

    api::JsonReader display = api::memberValue(item, "display");
    long long width = 0, height = 0;
    if (!display.isNull()) {
      TEST_ASSERT_TRUE(display.enterArray());
      TEST_ASSERT_TRUE(display.nextElement());
      TEST_ASSERT_TRUE(display.asLong(width));
      display.skipValue();
      TEST_ASSERT_TRUE(display.nextElement());
      TEST_ASSERT_TRUE(display.asLong(height));
    }
    TEST_ASSERT_EQUAL_INT_MESSAGE(width, meta.displayWidth, name.c_str());
    TEST_ASSERT_EQUAL_INT_MESSAGE(height, meta.displayHeight, name.c_str());

    const std::vector<script::Requirement> requirements = script::splitRequires(meta.requirements);
    api::JsonReader expected = api::memberValue(item, "requires");
    TEST_ASSERT_TRUE(expected.enterArray());
    std::size_t i = 0;
    while (expected.nextElement()) {
      const api::JsonReader requirement = expected;
      std::string wantName, wantHub;
      api::memberValue(requirement, "name").appendString(wantName);
      const api::JsonReader hub = api::memberValue(requirement, "hub");
      if (!hub.isNull()) hub.appendString(wantHub);
      TEST_ASSERT_TRUE_MESSAGE(i < requirements.size(), name.c_str());
      TEST_ASSERT_EQUAL_STRING_MESSAGE(wantName.c_str(), requirements[i].name.c_str(), name.c_str());
      TEST_ASSERT_EQUAL_STRING_MESSAGE(wantHub.c_str(), requirements[i].hub.c_str(), name.c_str());
      ++i;
      expected.skipValue();
    }
    TEST_ASSERT_EQUAL_size_t_MESSAGE(i, requirements.size(), name.c_str());
    ++count;
    cases.skipValue();
  }
  TEST_ASSERT_TRUE(count > 10);
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
  RUN_TEST(test_ondemand_is_a_bare_flag_in_any_case);
  RUN_TEST(test_modules_and_headless_scripts_are_never_ondemand);
  RUN_TEST(test_config_lines_are_flagged_but_not_read);
  RUN_TEST(test_a_bare_config_line_still_counts);
  RUN_TEST(test_icons_one_line);
  RUN_TEST(test_several_icons_lines_are_one_list);
  RUN_TEST(test_icons_without_a_value_is_empty);
  RUN_TEST(test_no_icons_line_is_an_empty_list);
  RUN_TEST(test_duplicate_ids_appear_once);
  RUN_TEST(test_unusable_entries_are_dropped);
  RUN_TEST(test_an_overlong_id_is_dropped);
  RUN_TEST(test_the_list_is_capped);
  RUN_TEST(test_the_stored_text_is_bounded);
  RUN_TEST(test_requires_lines_list_names_and_hub_ids);
  RUN_TEST(test_unusable_requires_lines_are_dropped_whole);
  RUN_TEST(test_requires_are_capped);
  RUN_TEST(test_header_matches_the_shared_vectors);
  return UNITY_END();
}
