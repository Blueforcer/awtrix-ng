#include <unity.h>

#include <string>
#include <string_view>

#include "core/Settings.h"
#include "core/api/JsonReader.h"
#include "core/api/JsonWriter.h"

using namespace awtrix;

void setUp() {}
void tearDown() {}

namespace {

std::string serialize(const Settings& s) {
  std::string out;
  api::JsonWriter w(out);
  w.beginObject();
  s.writeMembers(w);
  w.endObject();
  return out;
}

std::string diffFromDefaults(const Settings& s) {
  const std::string was = serialize(Settings{});
  const std::string now = serialize(s);
  api::JsonReader a{std::string_view(was)}, b{std::string_view(now)};
  std::string out;
  if (!a.enterObject() || !b.enterObject()) return "WALK-FAILED";
  while (a.nextMember() && b.nextMember()) {
    const std::string key(b.key());
    const std::string_view av = a.valueText();
    const std::string_view bv = b.valueText();
    if (av != bv) out += key + "=" + std::string(bv) + ";";
    if (!a.skipValue() || !b.skipValue()) break;
  }
  return out;
}

std::string applied(const char* json) {
  Settings s;
  const int n = s.applyRead(api::JsonReader(json));
  return std::to_string(n) + " " + diffFromDefaults(s);
}

std::string validated(const char* json) {
  SettingsError err;
  if (Settings::validateRead(api::JsonReader(json), err)) return "ok";
  return err.field + "|" + err.message;
}

}

static void test_defaults_serialize_whole() {
  TEST_ASSERT_EQUAL_STRING(
      "{\"autoBrightness\":false,\"brightness\":120,\"autoTransition\":true,\"textColor\":"
      "\"#FFFFFF\",\"transitionEffect\":\"Rain\",\"transitionDurationMs\":1000,\"appDurationMs\":"
      "7000,\"timeMode\":1,\"calendarHeaderColor\":\"#FF0000\",\"calendarTextColor\":\"#000000\","
      "\"calendarBodyColor\":\"#FFFFFF\",\"time24h\":true,\"timeLeadingZero\":true,"
      "\"timeShowSeconds\":false,\"timeShowAmPm\":false,\"timeSeparatorMode\":\"pulse\","
      "\"dateOrder\":\"dayMonthYear\",\"dateSeparator\":\"dot\",\"dateYearMode\":\"twoDigit\","
      "\"dateShowWeekday\":false,\"dateMonthNames\":false,\"useCelsius\":true,\"blockNavigation\":"
      "false,\"soundEnabled\":true,"
      "\"uppercase\":true,\"timeColor\":null,\"dateColor\":null,"
      "\"humidityColor\":null,\"temperatureColor\":null,\"batteryColor\":null,"
      "\"buzzerVolume\":80,\"dfplayerVolume\":80,\"mp3Volume\":70,"
      "\"radioVolume\":60,\"radioMeta\":true,\"saturation\":100,\"gamma\":1.899999976,"
      "\"colorCorrection\":null,"
      "\"colorTint\":null,\"scroll\":{\"mode\":\"wrap\",\"direction\":\"left\",\"entry\":\"inline\","
      "\"whenFits\":\"static\",\"speed\":100,\"gap\":8,\"holdMs\":1000},\"weekdayBar\":{\"show\":true,"
      "\"startOnMonday\":true,\"weekendDays\":[\"sunday\",\"saturday\"],\"activeColor\":\"#FFFFFF\","
      "\"inactiveColor\":\"#666666\",\"weekendActiveColor\":\"#FFFFFF\",\"weekendInactiveColor\":"
      "\"#666666\"}}",
      serialize(Settings{}).c_str());
}

static void test_reply_prints_floats_at_nine_digits() {
  Settings s;
  s.gamma = 0.1f;
  TEST_ASSERT_EQUAL_STRING("gamma=0.100000001;", diffFromDefaults(s).c_str());
  s.gamma = 1.0f / 3.0f;
  TEST_ASSERT_EQUAL_STRING("gamma=0.333333343;", diffFromDefaults(s).c_str());
  s.gamma = 2.0f;
  TEST_ASSERT_EQUAL_STRING("gamma=2;", diffFromDefaults(s).c_str());
}

static void test_reply_prints_enums_by_name() {
  Settings s;
  s.timeSeparatorMode = kSepBlink;
  s.dateOrder = kDateOrderYMD;
  s.dateSeparator = kDateSepDash;
  s.dateYearMode = kYearFourDigit;
  s.transitionEffect = 0;
  TEST_ASSERT_EQUAL_STRING(
      "transitionEffect=\"Random\";timeSeparatorMode=\"blink\";dateOrder=\"yearMonthDay\";"
      "dateSeparator=\"dash\";dateYearMode=\"fourDigit\";",
      diffFromDefaults(s).c_str());
}

static void test_reply_clamps_an_out_of_table_enum_to_the_first_name() {
  Settings s;
  s.timeSeparatorMode = 99;
  s.dateYearMode = -1;
  s.transitionEffect = 9999;
  TEST_ASSERT_EQUAL_STRING(
      "transitionEffect=\"Random\";timeSeparatorMode=\"steady\";dateYearMode=\"none\";",
      diffFromDefaults(s).c_str());
}

static void test_reply_writes_an_unset_optional_colour_as_null() {
  Settings s;
  s.timeColor = OptColor{0x102030u, true};
  TEST_ASSERT_EQUAL_STRING("timeColor=\"#102030\";", diffFromDefaults(s).c_str());
  s.timeColor = OptColor{};
  TEST_ASSERT_EQUAL_STRING("", diffFromDefaults(s).c_str());
}

static void test_apply_counts_only_the_fields_it_took() {
  TEST_ASSERT_EQUAL_STRING("0 ", applied("{}").c_str());
  TEST_ASSERT_EQUAL_STRING("1 brightness=200;", applied("{\"brightness\":200}").c_str());
  TEST_ASSERT_EQUAL_STRING("2 brightness=200;buzzerVolume=7;",
                           applied("{\"brightness\":200,\"buzzerVolume\":7}").c_str());
  TEST_ASSERT_EQUAL_STRING("0 ", applied("{\"nonesuch\":1}").c_str());
}

static void test_apply_takes_the_default_when_the_type_is_wrong() {
  TEST_ASSERT_EQUAL_STRING("1 ", applied("{\"autoTransition\":\"yes\"}").c_str());
  TEST_ASSERT_EQUAL_STRING("1 brightness=0;", applied("{\"brightness\":\"loud\"}").c_str());
  TEST_ASSERT_EQUAL_STRING("1 appDurationMs=0;", applied("{\"appDurationMs\":false}").c_str());
  TEST_ASSERT_EQUAL_STRING("1 ", applied("{\"gamma\":\"nonsense\"}").c_str());
}

static void test_apply_clamps_an_integer_into_its_range() {
  TEST_ASSERT_EQUAL_STRING("1 brightness=255;", applied("{\"brightness\":9000}").c_str());
  TEST_ASSERT_EQUAL_STRING("1 brightness=0;", applied("{\"brightness\":-5}").c_str());
  TEST_ASSERT_EQUAL_STRING("1 ", applied("{\"saturation\":999}").c_str());
  TEST_ASSERT_EQUAL_STRING("1 saturation=0;", applied("{\"saturation\":-5}").c_str());
  TEST_ASSERT_EQUAL_STRING("1 appDurationMs=0;", applied("{\"appDurationMs\":-1}").c_str());
}

static void test_apply_skips_an_enum_it_cannot_name() {
  TEST_ASSERT_EQUAL_STRING("1 dateOrder=\"yearMonthDay\";",
                           applied("{\"dateOrder\":\"yearMonthDay\"}").c_str());
  TEST_ASSERT_EQUAL_STRING("1 dateOrder=\"yearMonthDay\";",
                           applied("{\"dateOrder\":\"YEARMONTHDAY\"}").c_str());
  TEST_ASSERT_EQUAL_STRING("0 ", applied("{\"dateOrder\":\"sideways\"}").c_str());
  TEST_ASSERT_EQUAL_STRING("0 ", applied("{\"dateOrder\":2}").c_str());
  TEST_ASSERT_EQUAL_STRING("1 transitionEffect=\"Zoom\";",
                           applied("{\"transitionEffect\":\"zoom\"}").c_str());
}

static void test_apply_reads_every_colour_form() {
  TEST_ASSERT_EQUAL_STRING("textColor=\"#102030\";",
                           applied("{\"textColor\":\"#102030\"}").substr(2).c_str());
  TEST_ASSERT_EQUAL_STRING("textColor=\"#112233\";",
                           applied("{\"textColor\":\"#123\"}").substr(2).c_str());
  TEST_ASSERT_EQUAL_STRING("textColor=\"#010203\";",
                           applied("{\"textColor\":[1,2,3]}").substr(2).c_str());
  TEST_ASSERT_EQUAL_STRING("textColor=\"#FF0000\";",
                           applied("{\"textColor\":[\"HSV\",0,255,255]}").substr(2).c_str());
  TEST_ASSERT_EQUAL_STRING("textColor=\"#00FF00\";",
                           applied("{\"textColor\":65280}").substr(2).c_str());
  TEST_ASSERT_EQUAL_STRING("0 ", applied("{\"textColor\":[1.5,2,3]}").c_str());
  TEST_ASSERT_EQUAL_STRING("0 ", applied("{\"textColor\":[\"hsv\",0,255,255]}").c_str());
  TEST_ASSERT_EQUAL_STRING("0 ", applied("{\"textColor\":\"chartreuse\"}").c_str());
  TEST_ASSERT_EQUAL_STRING("1 textColor=\"#000000\";", applied("{\"textColor\":-1}").c_str());
}

static void test_apply_clears_an_optional_colour_with_null() {
  TEST_ASSERT_EQUAL_STRING("1 timeColor=\"#00FF00\";", applied("{\"timeColor\":65280}").c_str());
  TEST_ASSERT_EQUAL_STRING("1 ", applied("{\"timeColor\":null}").c_str());
  TEST_ASSERT_EQUAL_STRING("0 ", applied("{\"timeColor\":\"nope\"}").c_str());
}

static void test_apply_overlays_a_partial_scroll_object() {
  TEST_ASSERT_EQUAL_STRING(
      "1 scroll={\"mode\":\"loop\",\"direction\":\"left\",\"entry\":\"inline\",\"whenFits\":"
      "\"static\",\"speed\":100,\"gap\":8,\"holdMs\":1000};",
      applied("{\"scroll\":{\"mode\":\"loop\"}}").c_str());
  TEST_ASSERT_EQUAL_STRING(
      "1 scroll={\"mode\":\"bounce\",\"direction\":\"left\",\"entry\":\"inline\",\"whenFits\":"
      "\"static\",\"speed\":100,\"gap\":8,\"holdMs\":1000};",
      applied("{\"scroll\":\"bounce\"}").c_str());
  TEST_ASSERT_EQUAL_STRING(
      "1 scroll={\"mode\":\"wrap\",\"direction\":\"right\",\"entry\":\"offscreen\",\"whenFits\":"
      "\"scroll\",\"speed\":40,\"gap\":2,\"holdMs\":300};",
      applied("{\"scroll\":{\"direction\":\"right\",\"entry\":\"offscreen\",\"whenFits\":"
              "\"scroll\",\"speed\":40,\"gap\":2,\"holdMs\":300}}")
          .c_str());
  TEST_ASSERT_EQUAL_STRING("0 ", applied("{\"scroll\":{\"mode\":\"sideways\"}}").c_str());
}

static void test_apply_overlays_a_partial_weekday_bar() {
  TEST_ASSERT_EQUAL_STRING(
      "1 weekdayBar={\"show\":false,\"startOnMonday\":true,\"weekendDays\":[\"sunday\","
      "\"saturday\"],\"activeColor\":\"#FFFFFF\",\"inactiveColor\":\"#666666\","
      "\"weekendActiveColor\":\"#FFFFFF\",\"weekendInactiveColor\":\"#666666\"};",
      applied("{\"weekdayBar\":{\"show\":false}}").c_str());
  TEST_ASSERT_EQUAL_STRING(
      "1 weekdayBar={\"show\":true,\"startOnMonday\":true,\"weekendDays\":[\"friday\"],"
      "\"activeColor\":\"#FFFFFF\",\"inactiveColor\":\"#666666\",\"weekendActiveColor\":"
      "\"#FFFFFF\",\"weekendInactiveColor\":\"#666666\"};",
      applied("{\"weekdayBar\":{\"weekendDays\":[\"friday\"]}}").c_str());
  TEST_ASSERT_EQUAL_STRING(
      "0 weekdayBar={\"show\":false,\"startOnMonday\":true,\"weekendDays\":[\"sunday\","
      "\"saturday\"],\"activeColor\":\"#FFFFFF\",\"inactiveColor\":\"#666666\","
      "\"weekendActiveColor\":\"#FFFFFF\",\"weekendInactiveColor\":\"#666666\"};",
      applied("{\"weekdayBar\":{\"show\":false,\"nonesuch\":1}}").c_str());
}

static void test_apply_reads_a_numeric_string_as_a_float() {
  TEST_ASSERT_EQUAL_STRING("1 gamma=2.5;", applied("{\"gamma\":\"2.5\"}").c_str());
  TEST_ASSERT_EQUAL_STRING("1 gamma=2.5;", applied("{\"gamma\":2.5}").c_str());
  TEST_ASSERT_EQUAL_STRING("1 ", applied("{\"gamma\":0}").c_str());
  TEST_ASSERT_EQUAL_STRING("1 ", applied("{\"gamma\":-3}").c_str());
}

static void test_validate_names_the_first_offending_field() {
  TEST_ASSERT_EQUAL_STRING("ok", validated("{}").c_str());
  TEST_ASSERT_EQUAL_STRING("ok", validated("{\"brightness\":10,\"mp3Volume\":3}").c_str());
  TEST_ASSERT_EQUAL_STRING("nonesuch|unknown field", validated("{\"nonesuch\":1}").c_str());
  TEST_ASSERT_EQUAL_STRING("brightness|must be an integer",
                           validated("{\"brightness\":\"x\",\"mp3Volume\":\"y\"}").c_str());
  TEST_ASSERT_EQUAL_STRING("mp3Volume|must be an integer",
                           validated("{\"mp3Volume\":\"y\",\"brightness\":\"x\"}").c_str());
}

static void test_validate_rejects_a_bool_where_an_integer_belongs() {
  TEST_ASSERT_EQUAL_STRING("brightness|must be an integer",
                           validated("{\"brightness\":true}").c_str());
  TEST_ASSERT_EQUAL_STRING("appDurationMs|must be a non-negative integer (milliseconds)",
                           validated("{\"appDurationMs\":true}").c_str());
  TEST_ASSERT_EQUAL_STRING("brightness|must be an integer",
                           validated("{\"brightness\":1.5}").c_str());
  TEST_ASSERT_EQUAL_STRING("autoBrightness|must be a boolean",
                           validated("{\"autoBrightness\":1}").c_str());
}

static void test_validate_checks_the_range() {
  TEST_ASSERT_EQUAL_STRING("brightness|out of range", validated("{\"brightness\":256}").c_str());
  TEST_ASSERT_EQUAL_STRING("brightness|out of range", validated("{\"brightness\":-1}").c_str());
  TEST_ASSERT_EQUAL_STRING("ok", validated("{\"brightness\":255}").c_str());
  TEST_ASSERT_EQUAL_STRING("buzzerVolume|out of range",
                           validated("{\"buzzerVolume\":101}").c_str());
  // The single 0-30 key is gone, not renamed in place.
  TEST_ASSERT_EQUAL_STRING("volume|unknown field", validated("{\"volume\":10}").c_str());
  TEST_ASSERT_EQUAL_STRING("appDurationMs|must be a non-negative integer (milliseconds)",
                           validated("{\"appDurationMs\":-1}").c_str());
}

static void test_validate_wants_a_positive_number_for_gamma() {
  TEST_ASSERT_EQUAL_STRING("ok", validated("{\"gamma\":2.2}").c_str());
  TEST_ASSERT_EQUAL_STRING("ok", validated("{\"gamma\":2}").c_str());
  TEST_ASSERT_EQUAL_STRING("gamma|must be a positive number", validated("{\"gamma\":0}").c_str());
  TEST_ASSERT_EQUAL_STRING("gamma|must be a positive number", validated("{\"gamma\":-1}").c_str());
  TEST_ASSERT_EQUAL_STRING("gamma|must be a positive number",
                           validated("{\"gamma\":\"2.5\"}").c_str());
  TEST_ASSERT_EQUAL_STRING("gamma|must be a positive number",
                           validated("{\"gamma\":\"nonsense\"}").c_str());
}

static void test_validate_lists_the_choices_for_an_enum() {
  TEST_ASSERT_EQUAL_STRING("dateOrder|must be one of: dayMonthYear monthDayYear yearMonthDay",
                           validated("{\"dateOrder\":\"sideways\"}").c_str());
  TEST_ASSERT_EQUAL_STRING("timeSeparatorMode|must be one of: steady blink pulse",
                           validated("{\"timeSeparatorMode\":2}").c_str());
  TEST_ASSERT_EQUAL_STRING(
      "transitionEffect|must be one of: Random, Slide, Dim, Zoom, Rotate, Pixelate, Curtain, "
      "Ripple, Blink, Reload, Fade, Cover, Uncover, Split, Blinds, Blocks, Flash, Diamond, Wave, "
      "Rain, Melt, Interlace",
      validated("{\"transitionEffect\":\"warp\"}").c_str());
}

static void test_validate_describes_a_colour() {
  TEST_ASSERT_EQUAL_STRING(
      "textColor|must be a color (\"#RGB\", \"#RRGGBB\", [r,g,b], [\"HSV\",h,s,v] or a packed "
      "integer)",
      validated("{\"textColor\":\"chartreuse\"}").c_str());
  TEST_ASSERT_EQUAL_STRING("timeColor|must be a color or null",
                           validated("{\"timeColor\":\"chartreuse\"}").c_str());
  TEST_ASSERT_EQUAL_STRING("ok", validated("{\"timeColor\":null}").c_str());
  TEST_ASSERT_EQUAL_STRING("ok", validated("{\"textColor\":[\"HSV\",10,20,30]}").c_str());
}

static void test_validate_reports_a_nested_field_by_its_path() {
  TEST_ASSERT_EQUAL_STRING("ok", validated("{\"scroll\":{\"mode\":\"loop\"}}").c_str());
  TEST_ASSERT_EQUAL_STRING("scroll.mode|unknown value",
                           validated("{\"scroll\":{\"mode\":\"sideways\"}}").c_str());
  TEST_ASSERT_EQUAL_STRING("scroll.nonesuch|unknown field",
                           validated("{\"scroll\":{\"nonesuch\":1}}").c_str());
  TEST_ASSERT_EQUAL_STRING("scroll.speed|must be a non-negative integer",
                           validated("{\"scroll\":{\"speed\":true}}").c_str());
  TEST_ASSERT_EQUAL_STRING("scroll|must be an object or a mode string",
                           validated("{\"scroll\":42}").c_str());
  TEST_ASSERT_EQUAL_STRING("weekdayBar.nonesuch|unknown field",
                           validated("{\"weekdayBar\":{\"nonesuch\":1}}").c_str());
  TEST_ASSERT_EQUAL_STRING("weekdayBar.weekendDays|must be an array of weekday names",
                           validated("{\"weekdayBar\":{\"weekendDays\":[\"caturday\"]}}").c_str());
  TEST_ASSERT_EQUAL_STRING("weekdayBar|must be an object",
                           validated("{\"weekdayBar\":\"yes\"}").c_str());
}

static void test_validate_beats_a_malformed_value_with_an_unknown_key() {
  TEST_ASSERT_EQUAL_STRING("brightness|must be an integer",
                           validated("{\"brightness\":\"x\",\"nonesuch\":1}").c_str());
  TEST_ASSERT_EQUAL_STRING("nonesuch|unknown field",
                           validated("{\"nonesuch\":1,\"brightness\":\"x\"}").c_str());
}

static void test_the_reply_reads_back_into_the_same_state() {
  for (int i = -1; i < 25; ++i) {
    Settings src;
    src.transitionEffect = i;
    src.timeSeparatorMode = i;
    src.dateOrder = i;
    src.dateSeparator = i;
    src.dateYearMode = i;
    src.brightness = 37;
    src.gamma = 2.35f;
    src.timeColor = OptColor{0x00FF00u, true};
    src.scrollDefaults.mode = ScrollMode::Bounce;
    src.weekdayBar.weekendMask = 1u << 3;

    const std::string body = serialize(src);
    SettingsError err;
    TEST_ASSERT_TRUE_MESSAGE(Settings::validateRead(api::JsonReader(body), err),
                             err.field.c_str());
    Settings back;
    back.applyRead(api::JsonReader(body));
    TEST_ASSERT_EQUAL_STRING(body.c_str(), serialize(back).c_str());
  }
}

static void test_an_escaped_key_does_not_reach_its_field() {
  const char* json = "{\"\\u0062rightness\":200}";
  TEST_ASSERT_EQUAL_STRING("0 ", applied(json).c_str());
  TEST_ASSERT_EQUAL_STRING("\\u0062rightness|unknown field", validated(json).c_str());
}

static void test_a_body_that_is_not_an_object_is_a_no_op() {
  for (const char* body : {"[1,2]", "42", "null", "\"settings\""}) {
    TEST_ASSERT_EQUAL_STRING_MESSAGE("0 ", applied(body).c_str(), body);
    TEST_ASSERT_EQUAL_STRING_MESSAGE("ok", validated(body).c_str(), body);
  }
}

int main(int, char**) {
  UNITY_BEGIN();
  RUN_TEST(test_defaults_serialize_whole);
  RUN_TEST(test_reply_prints_floats_at_nine_digits);
  RUN_TEST(test_reply_prints_enums_by_name);
  RUN_TEST(test_reply_clamps_an_out_of_table_enum_to_the_first_name);
  RUN_TEST(test_reply_writes_an_unset_optional_colour_as_null);
  RUN_TEST(test_apply_counts_only_the_fields_it_took);
  RUN_TEST(test_apply_takes_the_default_when_the_type_is_wrong);
  RUN_TEST(test_apply_clamps_an_integer_into_its_range);
  RUN_TEST(test_apply_skips_an_enum_it_cannot_name);
  RUN_TEST(test_apply_reads_every_colour_form);
  RUN_TEST(test_apply_clears_an_optional_colour_with_null);
  RUN_TEST(test_apply_overlays_a_partial_scroll_object);
  RUN_TEST(test_apply_overlays_a_partial_weekday_bar);
  RUN_TEST(test_apply_reads_a_numeric_string_as_a_float);
  RUN_TEST(test_validate_names_the_first_offending_field);
  RUN_TEST(test_validate_rejects_a_bool_where_an_integer_belongs);
  RUN_TEST(test_validate_checks_the_range);
  RUN_TEST(test_validate_wants_a_positive_number_for_gamma);
  RUN_TEST(test_validate_lists_the_choices_for_an_enum);
  RUN_TEST(test_validate_describes_a_colour);
  RUN_TEST(test_validate_reports_a_nested_field_by_its_path);
  RUN_TEST(test_validate_beats_a_malformed_value_with_an_unknown_key);
  RUN_TEST(test_the_reply_reads_back_into_the_same_state);
  RUN_TEST(test_an_escaped_key_does_not_reach_its_field);
  RUN_TEST(test_a_body_that_is_not_an_object_is_a_no_op);
  return UNITY_END();
}
