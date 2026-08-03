#include <string>

#include <unity.h>

#include "core/script/ScriptConfig.h"
#include "core/script/ScriptMeta.h"

using namespace awtrix;
using awtrix::script::ConfigSchema;
using awtrix::script::ConfigType;

void setUp() {}
void tearDown() {}

static const char* kWeather =
    "# @name    Weather\n"
    "# @config  city   text   \"City\"     default=\"Berlin\" maxlen=48\n"
    "# @config  metric bool   \"Celsius\"           default=true\n"
    "# @config  every  number \"Refresh\"  default=15 min=1 max=60 unit=min\n"
    "# @config  mode   select \"Mode\"     default=today options=now,today,week\n"
    "# @config  tint   color  \"Accent\"   default=#FF8800\n"
    "class Weather\n"
    "end\n";

static void test_parses_every_type() {
  const ConfigSchema s = script::parseConfig(kWeather);
  TEST_ASSERT_EQUAL_size_t(0, s.warnings.size());
  TEST_ASSERT_EQUAL_size_t(5, s.fields.size());

  TEST_ASSERT_EQUAL_STRING("city", s.fields[0].key.c_str());
  TEST_ASSERT_EQUAL(ConfigType::Text, s.fields[0].type);
  TEST_ASSERT_EQUAL_STRING("City", s.fields[0].label.c_str());
  TEST_ASSERT_EQUAL_STRING("\"Berlin\"", s.fields[0].defJson.c_str());
  TEST_ASSERT_EQUAL_size_t(48, s.fields[0].maxLen);

  TEST_ASSERT_EQUAL(ConfigType::Bool, s.fields[1].type);
  TEST_ASSERT_EQUAL_STRING("true", s.fields[1].defJson.c_str());

  TEST_ASSERT_EQUAL(ConfigType::Number, s.fields[2].type);
  TEST_ASSERT_EQUAL_STRING("15", s.fields[2].defJson.c_str());
  TEST_ASSERT_TRUE(s.fields[2].hasMin && s.fields[2].hasMax);
  TEST_ASSERT_EQUAL_STRING("min", s.fields[2].unit.c_str());

  TEST_ASSERT_EQUAL(ConfigType::Select, s.fields[3].type);
  TEST_ASSERT_EQUAL_STRING("now,today,week", s.fields[3].options.c_str());
  TEST_ASSERT_EQUAL_STRING("\"today\"", s.fields[3].defJson.c_str());

  TEST_ASSERT_EQUAL(ConfigType::Color, s.fields[4].type);
  TEST_ASSERT_EQUAL_STRING("16746496", s.fields[4].defJson.c_str());
}

static void test_meta_flags_config_without_parsing_it() {
  TEST_ASSERT_TRUE(script::parseMeta(kWeather).hasConfig);
  TEST_ASSERT_FALSE(script::parseMeta("# @name Plain\nclass P\nend\n").hasConfig);
}

static void test_label_is_optional_and_falls_back_to_the_key() {
  const ConfigSchema s = script::parseConfig("# @config city text default=\"Rom\"\n");
  TEST_ASSERT_EQUAL_size_t(1, s.fields.size());
  TEST_ASSERT_EQUAL_STRING("city", s.fields[0].label.c_str());
}

static void test_quoted_attribute_keeps_its_spaces() {
  const ConfigSchema s = script::parseConfig(
      "# @config city text \"City\" default=\"New York\" help=\"Where you live\"\n");
  TEST_ASSERT_EQUAL_STRING("\"New York\"", s.fields[0].defJson.c_str());
  TEST_ASSERT_EQUAL_STRING("Where you live", s.fields[0].help.c_str());
}

static void test_bad_lines_warn_and_do_not_stop_the_rest() {
  const ConfigSchema s = script::parseConfig(
      "# @config one boolean \"A\"\n"
      "# @config 9bad text \"B\"\n"
      "# @config good text \"C\" default=\"x\"\n"
      "# @config good text \"D\"\n"
      "# @config sel select \"E\"\n");
  TEST_ASSERT_EQUAL_size_t(1, s.fields.size());
  TEST_ASSERT_EQUAL_STRING("good", s.fields[0].key.c_str());
  TEST_ASSERT_EQUAL_size_t(4, s.warnings.size());
  TEST_ASSERT_EQUAL_STRING("line 1: one: unknown type 'boolean', use bool, text, number, "
                           "slider, select or color",
                           s.warnings[0].c_str());
}

static void test_a_typo_in_a_range_is_reported_not_swallowed() {
  const ConfigSchema s = script::parseConfig(
      "# @config a slider \"A\" min=one max=8\n"
      "# @config b text   \"B\" maxlen=lots\n");
  TEST_ASSERT_EQUAL_size_t(2, s.fields.size());
  TEST_ASSERT_EQUAL_size_t(2, s.warnings.size());
  TEST_ASSERT_EQUAL_STRING("line 1: a: min is not a number", s.warnings[0].c_str());
  TEST_ASSERT_EQUAL_STRING("line 2: b: maxlen is not a length", s.warnings[1].c_str());
}

static void test_field_count_is_capped() {
  std::string src;
  for (int i = 0; i < 20; ++i)
    src += "# @config k" + std::to_string(i) + " text\n";
  const ConfigSchema s = script::parseConfig(src);
  TEST_ASSERT_EQUAL_size_t(script::kMaxConfigFields, s.fields.size());
  TEST_ASSERT_EQUAL_size_t(1, s.warnings.size());
}

// A module owns settings the same way an app does -- that is how several apps
// come to share one value: they import the module that holds it.
static void test_a_module_keeps_its_settings() {
  const ConfigSchema s = script::parseConfig("# @module fmt\n# @config a text default=\"x\"\n");
  TEST_ASSERT_EQUAL_size_t(1, s.fields.size());
  TEST_ASSERT_EQUAL_STRING("a", s.fields[0].key.c_str());
  TEST_ASSERT_EQUAL_size_t(0, s.warnings.size());
}

static void test_colour_accepts_hash_and_0x_and_decimal() {
  const ConfigSchema s = script::parseConfig(
      "# @config a color default=#FF8800\n"
      "# @config b color default=0xff8800\n"
      "# @config c color default=16746496\n"
      "# @config d color default=nonsense\n");
  TEST_ASSERT_EQUAL_STRING("16746496", s.fields[0].defJson.c_str());
  TEST_ASSERT_EQUAL_STRING("16746496", s.fields[1].defJson.c_str());
  TEST_ASSERT_EQUAL_STRING("16746496", s.fields[2].defJson.c_str());
  TEST_ASSERT_EQUAL_STRING("0", s.fields[3].defJson.c_str());
  TEST_ASSERT_EQUAL_size_t(1, s.warnings.size());
}

static void test_seeding_fills_only_what_is_missing() {
  const ConfigSchema s = script::parseConfig(kWeather);
  std::string out;
  TEST_ASSERT_TRUE(script::seedConfigDefaults(s, "{\"city\":\"Rom\",\"hits\":7}", out));
  TEST_ASSERT_EQUAL_STRING(
      "{\"city\":\"Rom\",\"hits\":7,\"metric\":true,\"every\":15,\"mode\":\"today\","
      "\"tint\":16746496}",
      out.c_str());
}

static void test_seeding_is_skipped_when_the_store_is_complete() {
  const ConfigSchema s = script::parseConfig("# @config a text default=\"x\"\n");
  std::string out;
  TEST_ASSERT_FALSE(script::seedConfigDefaults(s, "{\"a\":\"y\",\"other\":1}", out));
}

static void test_seeding_replaces_a_value_of_the_wrong_type() {
  const ConfigSchema s = script::parseConfig("# @config a bool default=true\n");
  std::string out;
  TEST_ASSERT_TRUE(script::seedConfigDefaults(s, "{\"a\":\"yes\",\"b\":2}", out));
  TEST_ASSERT_EQUAL_STRING("{\"a\":true,\"b\":2}", out.c_str());
}

static void test_seeding_survives_a_corrupt_store() {
  const ConfigSchema s = script::parseConfig("# @config a text default=\"x\"\n");
  std::string out;
  TEST_ASSERT_TRUE(script::seedConfigDefaults(s, "not json", out));
  TEST_ASSERT_EQUAL_STRING("{\"a\":\"x\"}", out.c_str());
}

static void test_a_setting_the_source_dropped_is_cleared_from_the_store() {
  const ConfigSchema before = script::parseConfig(kWeather);
  const ConfigSchema after = script::parseConfig(
      "# @config metric bool default=true\n"
      "# @config tint   color default=#FF8800\n");
  std::string out;
  TEST_ASSERT_TRUE(script::dropUndeclaredValues(
      before, after, "{\"city\":\"Rom\",\"hits\":7,\"metric\":false,\"tint\":1}", out));
  TEST_ASSERT_EQUAL_STRING("{\"hits\":7,\"metric\":false,\"tint\":1}", out.c_str());
}

static void test_a_key_the_script_owns_is_never_cleared() {
  const ConfigSchema before = script::parseConfig("# @config a text default=\"x\"\n");
  const ConfigSchema after = script::parseConfig("# @config a text default=\"x\"\n");
  std::string out;
  TEST_ASSERT_FALSE(script::dropUndeclaredValues(before, after, "{\"a\":\"y\",\"hits\":7}", out));

  const ConfigSchema none = script::parseConfig("class P\nend\n");
  TEST_ASSERT_FALSE(script::dropUndeclaredValues(none, none, "{\"hits\":7}", out));
}

static void test_renaming_a_setting_clears_the_old_one() {
  const ConfigSchema before = script::parseConfig("# @config city text default=\"Rom\"\n");
  const ConfigSchema after = script::parseConfig("# @config town text default=\"Rom\"\n");
  std::string out;
  TEST_ASSERT_TRUE(script::dropUndeclaredValues(before, after, "{\"city\":\"Wien\"}", out));
  TEST_ASSERT_EQUAL_STRING("{}", out.c_str());
}

static void test_json_reports_the_stored_value_and_the_default() {
  const ConfigSchema s = script::parseConfig(kWeather);
  std::string out;
  script::appendConfigJson(out, "Weather", s, "{\"city\":\"Rom\",\"every\":30}");
  TEST_ASSERT_TRUE(out.find("{\"name\":\"Weather\",\"fields\":[") == 0);
  TEST_ASSERT_TRUE(out.find("\"default\":\"Berlin\",\"value\":\"Rom\"") != std::string::npos);
  TEST_ASSERT_TRUE(out.find("\"default\":15,\"value\":30") != std::string::npos);
  TEST_ASSERT_TRUE(out.find("\"default\":true,\"value\":true") != std::string::npos);
  TEST_ASSERT_TRUE(out.find("\"options\":[\"now\",\"today\",\"week\"]") != std::string::npos);
  TEST_ASSERT_TRUE(out.find("\"unit\":\"min\"") != std::string::npos);
  TEST_ASSERT_TRUE(out.find("\"warnings\":[]}") != std::string::npos);
}

static void test_json_falls_back_when_the_stored_type_is_wrong() {
  const ConfigSchema s = script::parseConfig("# @config a bool default=true\n");
  std::string out;
  script::appendConfigJson(out, "S", s, "{\"a\":\"nope\"}");
  TEST_ASSERT_TRUE(out.find("\"default\":true,\"value\":true") != std::string::npos);
}

static void test_patch_merges_and_leaves_other_keys_alone() {
  const ConfigSchema s = script::parseConfig(kWeather);
  const script::ConfigPatch r =
      script::applyConfigPatch(s, "{\"city\":\"Berlin\",\"hits\":7,\"metric\":true}",
                               "{\"city\":\"Hamburg\",\"tint\":\"#00FF00\"}");
  TEST_ASSERT_TRUE(r.ok);
  TEST_ASSERT_EQUAL_STRING("{\"city\":\"Hamburg\",\"hits\":7,\"metric\":true,\"tint\":65280}",
                           r.storeJson.c_str());
}

static void test_patch_clamps_a_number_to_its_range() {
  const ConfigSchema s = script::parseConfig(kWeather);
  const script::ConfigPatch r = script::applyConfigPatch(s, "{}", "{\"every\":900}");
  TEST_ASSERT_TRUE(r.ok);
  TEST_ASSERT_EQUAL_STRING("{\"every\":60}", r.storeJson.c_str());
}

static void test_patch_rejects_an_unknown_key() {
  const ConfigSchema s = script::parseConfig(kWeather);
  const script::ConfigPatch r = script::applyConfigPatch(s, "{}", "{\"citty\":\"Rom\"}");
  TEST_ASSERT_FALSE(r.ok);
  TEST_ASSERT_EQUAL_STRING("citty", r.field.c_str());
}

static void test_patch_rejects_a_wrong_type_and_an_unoffered_option() {
  const ConfigSchema s = script::parseConfig(kWeather);
  const script::ConfigPatch bad = script::applyConfigPatch(s, "{}", "{\"metric\":\"yes\"}");
  TEST_ASSERT_FALSE(bad.ok);
  TEST_ASSERT_EQUAL_STRING("metric", bad.field.c_str());

  const script::ConfigPatch off = script::applyConfigPatch(s, "{}", "{\"mode\":\"yesterday\"}");
  TEST_ASSERT_FALSE(off.ok);
  TEST_ASSERT_EQUAL_STRING("mode", off.field.c_str());
}

static void test_patch_rejects_text_over_its_length() {
  const ConfigSchema s = script::parseConfig("# @config a text maxlen=4\n");
  const script::ConfigPatch r = script::applyConfigPatch(s, "{}", "{\"a\":\"toolong\"}");
  TEST_ASSERT_FALSE(r.ok);
  TEST_ASSERT_EQUAL_STRING("a", r.field.c_str());
}

static void test_patch_rejects_a_body_that_is_not_an_object() {
  const ConfigSchema s = script::parseConfig(kWeather);
  TEST_ASSERT_FALSE(script::applyConfigPatch(s, "{}", "[1,2]").ok);
  TEST_ASSERT_FALSE(script::applyConfigPatch(s, "{}", "{oops}").ok);
}

int main() {
  UNITY_BEGIN();
  RUN_TEST(test_parses_every_type);
  RUN_TEST(test_meta_flags_config_without_parsing_it);
  RUN_TEST(test_label_is_optional_and_falls_back_to_the_key);
  RUN_TEST(test_quoted_attribute_keeps_its_spaces);
  RUN_TEST(test_bad_lines_warn_and_do_not_stop_the_rest);
  RUN_TEST(test_a_typo_in_a_range_is_reported_not_swallowed);
  RUN_TEST(test_field_count_is_capped);
  RUN_TEST(test_a_module_keeps_its_settings);
  RUN_TEST(test_colour_accepts_hash_and_0x_and_decimal);
  RUN_TEST(test_seeding_fills_only_what_is_missing);
  RUN_TEST(test_seeding_is_skipped_when_the_store_is_complete);
  RUN_TEST(test_seeding_replaces_a_value_of_the_wrong_type);
  RUN_TEST(test_seeding_survives_a_corrupt_store);
  RUN_TEST(test_a_setting_the_source_dropped_is_cleared_from_the_store);
  RUN_TEST(test_a_key_the_script_owns_is_never_cleared);
  RUN_TEST(test_renaming_a_setting_clears_the_old_one);
  RUN_TEST(test_json_reports_the_stored_value_and_the_default);
  RUN_TEST(test_json_falls_back_when_the_stored_type_is_wrong);
  RUN_TEST(test_patch_merges_and_leaves_other_keys_alone);
  RUN_TEST(test_patch_clamps_a_number_to_its_range);
  RUN_TEST(test_patch_rejects_an_unknown_key);
  RUN_TEST(test_patch_rejects_a_wrong_type_and_an_unoffered_option);
  RUN_TEST(test_patch_rejects_text_over_its_length);
  RUN_TEST(test_patch_rejects_a_body_that_is_not_an_object);
  return UNITY_END();
}
