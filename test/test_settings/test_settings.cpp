#include <unity.h>

#include <string>

#include "core/Settings.h"
#include "core/api/JsonReader.h"
#include "core/api/JsonWriter.h"

using namespace awtrix;

void setUp() {}
void tearDown() {}

namespace {

class Body {
 public:
  Body& set(const char* k, const char* v) { return add(k, quoted(v)); }
  Body& set(const char* k, const std::string& v) { return add(k, quoted(v.c_str())); }
  Body& set(const char* k, long v) { return add(k, std::to_string(v)); }
  Body& set(const char* k, int v) { return add(k, std::to_string(v)); }
  Body& set(const char* k, bool v) { return add(k, v ? "true" : "false"); }
  Body& raw(const char* k, const char* json) { return add(k, json); }

  std::string str() const { return "{" + body_ + "}"; }
  int applyTo(Settings& s) const { return s.applyRead(api::JsonReader(str())); }
  bool validate(SettingsError& e) const {
    return Settings::validateRead(api::JsonReader(str()), e);
  }

 private:
  Body& add(const char* k, const std::string& v) {
    if (!body_.empty()) body_ += ',';
    body_ += std::string("\"") + k + "\":" + v;
    return *this;
  }
  static std::string quoted(const char* v) { return std::string("\"") + v + "\""; }
  std::string body_;
};

std::string reply(const Settings& s) {
  std::string out;
  api::JsonWriter w(out);
  w.beginObject();
  s.writeMembers(w);
  w.endObject();
  return out;
}

api::JsonReader at(const std::string& json, const char* key) {
  return api::memberValue(api::JsonReader(json), key);
}
bool hasKey(const std::string& json, const char* key) {
  return at(json, key).type() != api::JsonReader::Type::Invalid;
}
bool isNullAt(const std::string& json, const char* key) { return at(json, key).isNull(); }
std::string strAt(const std::string& json, const char* key) {
  std::string v;
  at(json, key).appendString(v);
  return v;
}
long intAt(const std::string& json, const char* key) {
  long long v = 0;
  at(json, key).asLong(v);
  return static_cast<long>(v);
}

}

static void test_defaults_unchanged() {
  Settings s;
  TEST_ASSERT_EQUAL_INT(120, s.brightness);
  TEST_ASSERT_TRUE(s.autoTransition);
  TEST_ASSERT_FALSE(s.autoBrightness);
  TEST_ASSERT_EQUAL_HEX32(0xFFFFFFu, s.textColor);
  TEST_ASSERT_EQUAL_INT(7000, (int)s.appDurationMs);
  TEST_ASSERT_TRUE(s.useCelsius);
  TEST_ASSERT_TRUE(s.soundEnabled);
  TEST_ASSERT_FALSE(s.blockNavigation);
}

static void test_toJson_camelCase_schema() {
  Settings s;
  s.appDurationMs = 7000;
  s.transitionEffect = 1;
  const std::string o = reply(s);
  TEST_ASSERT_EQUAL_INT(7000, static_cast<int>(intAt(o, "appDurationMs")));
  TEST_ASSERT_EQUAL_STRING("#FFFFFF", strAt(o, "textColor").c_str());
  TEST_ASSERT_EQUAL_STRING("Slide", strAt(o, "transitionEffect").c_str());
  TEST_ASSERT_TRUE(hasKey(o, "useCelsius"));
  TEST_ASSERT_TRUE(hasKey(o, "blockNavigation"));
  TEST_ASSERT_FALSE(hasKey(o, "ATIME"));
  TEST_ASSERT_FALSE(hasKey(o, "TCOL"));
}

static void test_toJson_nullable_colors_emit_null() {
  Settings s;
  const std::string o = reply(s);
  TEST_ASSERT_TRUE(hasKey(o, "timeColor"));
  TEST_ASSERT_TRUE(isNullAt(o, "timeColor"));
  TEST_ASSERT_TRUE(isNullAt(o, "colorCorrection"));
  s.timeColor = OptColor{0x112233u, true};
  s.colorCorrection = OptColor{0xAABBCCu, true};
  const std::string o2 = reply(s);
  TEST_ASSERT_EQUAL_STRING("#112233", strAt(o2, "timeColor").c_str());
  TEST_ASSERT_EQUAL_STRING("#AABBCC", strAt(o2, "colorCorrection").c_str());
}

static void test_applyJson_duration_is_plain_ms() {
  Settings s;
  Body d;
  d.set("appDurationMs", 10000);
  d.applyTo(s);
  TEST_ASSERT_EQUAL_INT(10000, (int)s.appDurationMs);
}

static void test_applyJson_colors_hex_array_hsv() {
  Settings s;
  Body d;
  d.set("textColor", "#FF0000");
  d.raw("calendarHeaderColor", "[0,255,0]");
  d.raw("calendarBodyColor", "[\"HSV\",0,100,100]");
  d.applyTo(s);
  TEST_ASSERT_EQUAL_HEX32(0xFF0000u, s.textColor);
  TEST_ASSERT_EQUAL_HEX32(0x00FF00u, s.calendarHeaderColor);
  TEST_ASSERT_EQUAL_HEX32(0xFF0000u, s.calendarBodyColor);
}

static void test_applyJson_nullable_null_resets_to_inherit() {
  Settings s;
  s.timeColor = OptColor{0x123456u, true};
  s.colorCorrection = OptColor{0x123456u, true};
  Body d;
  d.raw("timeColor", "null");
  d.raw("colorCorrection", "null");
  d.applyTo(s);
  TEST_ASSERT_FALSE(s.timeColor.set);
  TEST_ASSERT_FALSE(s.colorCorrection.set);
}

static void test_pure_black_and_white_are_representable() {
  Settings s;
  Body d;
  d.set("timeColor", "#000000");
  d.set("colorCorrection", "#FFFFFF");
  d.applyTo(s);
  TEST_ASSERT_TRUE(s.timeColor.set);
  TEST_ASSERT_EQUAL_HEX32(0x000000u, s.timeColor.rgb);
  TEST_ASSERT_TRUE(s.colorCorrection.set);
  TEST_ASSERT_EQUAL_HEX32(0xFFFFFFu, s.colorCorrection.rgb);

  const std::string oo = reply(s);
  TEST_ASSERT_EQUAL_STRING("#000000", strAt(oo, "timeColor").c_str());
  TEST_ASSERT_EQUAL_STRING("#FFFFFF", strAt(oo, "colorCorrection").c_str());
}

static void test_panel_keys_do_not_live_on_settings() {
  const std::string o = reply(Settings{});
  for (const char* key : {"panelWidth", "panels", "panelStart", "panelWiring", "panelSerpentine",
                          "panelChainReverse", "panelChainSerpentine", "matrixWidth",
                          "matrixLayout"})
    TEST_ASSERT_FALSE_MESSAGE(hasKey(o, key), key);

  Body d;
  d.set("panelWidth", 64);
  SettingsError e;
  TEST_ASSERT_FALSE(d.validate(e));
  TEST_ASSERT_EQUAL_STRING("panelWidth", e.field.c_str());
}

static void test_transitionEffect_by_name() {
  Settings s;
  Body d;
  d.set("transitionEffect", "Fade");
  d.applyTo(s);
  TEST_ASSERT_EQUAL_INT(10, s.transitionEffect);
}

static void test_clock_enums_names_on_the_wire() {
  Settings s;
  const std::string o = reply(s);
  TEST_ASSERT_EQUAL_STRING("pulse", strAt(o, "timeSeparatorMode").c_str());
  TEST_ASSERT_EQUAL_STRING("dayMonthYear", strAt(o, "dateOrder").c_str());
  TEST_ASSERT_EQUAL_STRING("dot", strAt(o, "dateSeparator").c_str());
  TEST_ASSERT_EQUAL_STRING("twoDigit", strAt(o, "dateYearMode").c_str());
  Body d2;
  d2.set("timeSeparatorMode", "pulse");
  d2.set("dateOrder", "yearMonthDay");
  d2.applyTo(s);
  TEST_ASSERT_EQUAL_INT(kSepPulse, s.timeSeparatorMode);
  TEST_ASSERT_EQUAL_INT(kDateOrderYMD, s.dateOrder);
  TEST_ASSERT_FALSE(hasKey(o, "timeFormat"));
  TEST_ASSERT_FALSE(hasKey(o, "dateFormat"));
}

static void test_validate_bad_enum_value() {
  Body d;
  d.set("timeSeparatorMode", "strobe");
  SettingsError e;
  TEST_ASSERT_FALSE(d.validate(e));
  TEST_ASSERT_EQUAL_STRING("timeSeparatorMode", e.field.c_str());
  TEST_ASSERT_EQUAL_STRING("must be one of: steady blink pulse", e.message.c_str());
}

static void test_validate_accepts_valid_payload() {
  Body d;
  d.set("brightness", 200);
  d.set("useCelsius", false);
  d.set("textColor", "#AABBCC");
  d.set("transitionEffect", "Zoom");
  SettingsError e;
  TEST_ASSERT_TRUE(d.validate(e));
}

static void test_validate_brightness_range() {
  Body d;
  d.set("brightness", 300);
  SettingsError e;
  TEST_ASSERT_FALSE(d.validate(e));
  TEST_ASSERT_EQUAL_STRING("brightness", e.field.c_str());
}

// One key per output, all four on the same 0-100 scale.
static void test_validate_volume_range() {
  SettingsError e;
  for (const char* key : {"buzzerVolume", "dfplayerVolume", "mp3Volume", "radioVolume"}) {
    Body dOk;
    dOk.set(key, 100);
    TEST_ASSERT_TRUE_MESSAGE(dOk.validate(e), e.field.c_str());

    Body dOver;
    dOver.set(key, 101);
    TEST_ASSERT_FALSE_MESSAGE(dOver.validate(e), key);
    TEST_ASSERT_EQUAL_STRING(key, e.field.c_str());
  }
}

// The old single key is gone rather than quietly accepted, so a stale client hears about it.
static void test_the_old_volume_key_is_rejected() {
  Body d;
  d.set("volume", 20);
  SettingsError e;
  TEST_ASSERT_FALSE(d.validate(e));
}

static void test_validate_type_mismatch() {
  Body d;
  d.set("brightness", "hell");
  SettingsError e;
  TEST_ASSERT_FALSE(d.validate(e));
  TEST_ASSERT_EQUAL_STRING("brightness", e.field.c_str());
}

static void test_validate_unknown_key_rejected() {
  Body d;
  d.set("ABRI", true);
  SettingsError e;
  TEST_ASSERT_FALSE(d.validate(e));
  TEST_ASSERT_EQUAL_STRING("ABRI", e.field.c_str());
}

static void test_validate_bad_transition_name() {
  Body d;
  d.set("transitionEffect", "Wobble");
  SettingsError e;
  TEST_ASSERT_FALSE(d.validate(e));
  TEST_ASSERT_EQUAL_STRING("transitionEffect", e.field.c_str());
  TEST_ASSERT_TRUE(e.message.find("must be one of") != std::string::npos);
}

static void test_names_are_case_insensitive() {
  Settings s;
  Body d;
  d.set("transitionEffect", "slide");
  d.set("timeSeparatorMode", "BLINK");
  SettingsError e;
  TEST_ASSERT_TRUE_MESSAGE(d.validate(e), e.field.c_str());
  d.applyTo(s);
  TEST_ASSERT_EQUAL_INT(1, s.transitionEffect);
  TEST_ASSERT_EQUAL_INT(1, s.timeSeparatorMode);
}

static void test_validate_timeMode_range() {
  Body d;
  d.set("timeMode", 6);
  SettingsError e;
  TEST_ASSERT_TRUE_MESSAGE(d.validate(e), e.field.c_str());
  Body d2;
  d2.set("timeMode", 7);
  TEST_ASSERT_FALSE(d2.validate(e));
  TEST_ASSERT_EQUAL_STRING("timeMode", e.field.c_str());
}

static void test_applyJson_clamps_out_of_range_on_load() {
  Settings s;
  Body d;
  d.set("brightness", 9999);
  d.set("timeMode", 99);
  d.set("buzzerVolume", -4);
  d.set("saturation", 140);
  d.applyTo(s);
  TEST_ASSERT_EQUAL_INT(255, s.brightness);
  TEST_ASSERT_EQUAL_INT(6, s.timeMode);
  TEST_ASSERT_EQUAL_INT(0, s.buzzerVolume);
  TEST_ASSERT_EQUAL_INT(100, s.saturation);
}

static void test_validate_saturation_range() {
  Body d;
  d.set("saturation", 101);
  SettingsError e;
  TEST_ASSERT_FALSE(d.validate(e));
  TEST_ASSERT_EQUAL_STRING("saturation", e.field.c_str());

  Body ok;
  ok.set("saturation", 0);
  TEST_ASSERT_TRUE_MESSAGE(ok.validate(e), e.field.c_str());
}

static void test_validate_bad_color() {
  Body d;
  d.set("textColor", "notacolor");
  SettingsError e;
  TEST_ASSERT_FALSE(d.validate(e));
  TEST_ASSERT_EQUAL_STRING("textColor", e.field.c_str());
}

static void test_validate_null_only_on_nullable_colors() {
  Body d;
  d.raw("timeColor", "null");
  SettingsError e;
  TEST_ASSERT_TRUE(d.validate(e));
  Body d2;
  d2.raw("textColor", "null");
  TEST_ASSERT_FALSE(d2.validate(e));
  TEST_ASSERT_EQUAL_STRING("textColor", e.field.c_str());
}

static void test_json_roundtrip_covers_every_field() {
  Settings a;
  a.autoBrightness = true;
  a.brightness = 42;
  a.autoTransition = false;
  a.textColor = 0x123456u;
  a.transitionEffect = 7;
  a.transitionDurationMs = 900;
  a.appDurationMs = 12000;
  a.timeMode = 3;
  a.calendarHeaderColor = 0x111111u;
  a.calendarTextColor = 0x222222u;
  a.calendarBodyColor = 0x333333u;
  a.time24h = false;
  a.timeLeadingZero = false;
  a.timeShowSeconds = true;
  a.timeShowAmPm = true;
  a.timeSeparatorMode = kSepPulse;
  a.dateOrder = kDateOrderMDY;
  a.dateSeparator = kDateSepSlash;
  a.dateYearMode = kYearFourDigit;
  a.dateShowWeekday = true;
  a.dateMonthNames = true;
  a.weekdayBar.startOnMonday = false;
  a.useCelsius = false;
  a.blockNavigation = true;
  a.soundEnabled = false;
  a.uppercase = false;
  a.weekdayBar.show = false;
  a.weekdayBar.activeColor = 0x444444u;
  a.weekdayBar.inactiveColor = 0x555555u;
  a.timeColor = OptColor{0x666666u, true};
  a.dateColor = OptColor{0x777777u, true};
  a.humidityColor = OptColor{0x888888u, true};
  a.temperatureColor = OptColor{0x999999u, true};
  a.batteryColor = OptColor{0xAAAAAAu, true};
  a.scrollDefaults.speed = 250;
  a.scrollDefaults.mode = ScrollMode::Bounce;
  a.buzzerVolume = 17;
  a.dfplayerVolume = 42;
  a.mp3Volume = 91;
  a.radioVolume = 33;
  a.saturation = 65;
  a.gamma = 2.4f;
  a.colorCorrection = OptColor{0xBBCCDDu, true};
  a.colorTint = OptColor{0xEEDDCCu, true};

  const std::string j1 = reply(a);

  SettingsError e;
  TEST_ASSERT_TRUE_MESSAGE(Settings::validateRead(api::JsonReader(j1), e), e.field.c_str());

  Settings b;
  b.applyRead(api::JsonReader(j1));

  TEST_ASSERT_EQUAL_STRING(j1.c_str(), reply(b).c_str());
}

static void test_canonicalKey_resolves_to_the_table_literal() {
  const char* k = Settings::canonicalKey("brightness");
  TEST_ASSERT_NOT_NULL(k);
  TEST_ASSERT_EQUAL_STRING("brightness", k);
  TEST_ASSERT_EQUAL_STRING("appDurationMs", Settings::canonicalKey("appDurationMs"));
  TEST_ASSERT_EQUAL_STRING("timeColor", Settings::canonicalKey("timeColor"));
}

static void test_canonicalKey_rejects_what_is_not_a_flat_field() {
  TEST_ASSERT_NULL(Settings::canonicalKey("nonesuch"));
  TEST_ASSERT_NULL(Settings::canonicalKey(""));
  TEST_ASSERT_NULL(Settings::canonicalKey("Brightness"));
  TEST_ASSERT_NULL(Settings::canonicalKey("brightness\":1,\"autoBrightness"));
  TEST_ASSERT_NULL(Settings::canonicalKey("scroll"));
  TEST_ASSERT_NULL(Settings::canonicalKey("weekdayBar"));
}

static void test_read_answers_every_field_kind() {
  Settings s;
  s.uppercase = false;
  s.brightness = 200;
  s.appDurationMs = 4500;
  s.gamma = 2.5f;
  s.timeSeparatorMode = kSepBlink;
  s.transitionEffect = 10;
  s.textColor = 0x00FF80u;

  TEST_ASSERT_EQUAL_INT(static_cast<int>(SettingValue::Type::Bool),
                        static_cast<int>(s.read("uppercase").type));
  TEST_ASSERT_FALSE(s.read("uppercase").b);
  TEST_ASSERT_EQUAL_INT(static_cast<int>(SettingValue::Type::Int),
                        static_cast<int>(s.read("brightness").type));
  TEST_ASSERT_EQUAL_INT(200, s.read("brightness").i);
  TEST_ASSERT_EQUAL_INT(static_cast<int>(SettingValue::Type::Int),
                        static_cast<int>(s.read("appDurationMs").type));
  TEST_ASSERT_EQUAL_INT(4500, s.read("appDurationMs").i);
  TEST_ASSERT_EQUAL_INT(static_cast<int>(SettingValue::Type::Real),
                        static_cast<int>(s.read("gamma").type));
  TEST_ASSERT_EQUAL_FLOAT(2.5f, s.read("gamma").f);
  TEST_ASSERT_EQUAL_INT(static_cast<int>(SettingValue::Type::Text),
                        static_cast<int>(s.read("timeSeparatorMode").type));
  TEST_ASSERT_EQUAL_STRING("blink", s.read("timeSeparatorMode").s);
  TEST_ASSERT_EQUAL_STRING("Fade", s.read("transitionEffect").s);
  TEST_ASSERT_EQUAL_INT(static_cast<int>(SettingValue::Type::Int),
                        static_cast<int>(s.read("textColor").type));
  TEST_ASSERT_EQUAL_INT(0x00FF80, s.read("textColor").i);
}

static void test_read_has_no_value_for_unset_or_unknown() {
  Settings s;
  TEST_ASSERT_FALSE(s.read("timeColor").has());
  TEST_ASSERT_FALSE(s.read("colorTint").has());
  TEST_ASSERT_FALSE(s.read("nonesuch").has());
  TEST_ASSERT_FALSE(s.read("scroll").has());
  s.timeColor = OptColor{0xFF0000u, true};
  TEST_ASSERT_TRUE(s.read("timeColor").has());
  TEST_ASSERT_EQUAL_INT(0xFF0000, s.read("timeColor").i);
}

static void test_read_follows_an_applied_patch() {
  Settings s;
  s.applyRead(api::JsonReader(
      "{\"brightness\":33,\"timeSeparatorMode\":\"steady\",\"dateColor\":\"#102030\"}"));
  TEST_ASSERT_EQUAL_INT(33, s.read("brightness").i);
  TEST_ASSERT_EQUAL_STRING("steady", s.read("timeSeparatorMode").s);
  TEST_ASSERT_EQUAL_INT(0x102030, s.read("dateColor").i);
}

int main(int, char**) {
  UNITY_BEGIN();
  RUN_TEST(test_json_roundtrip_covers_every_field);
  RUN_TEST(test_defaults_unchanged);
  RUN_TEST(test_toJson_camelCase_schema);
  RUN_TEST(test_toJson_nullable_colors_emit_null);
  RUN_TEST(test_applyJson_duration_is_plain_ms);
  RUN_TEST(test_applyJson_colors_hex_array_hsv);
  RUN_TEST(test_applyJson_nullable_null_resets_to_inherit);
  RUN_TEST(test_pure_black_and_white_are_representable);
  RUN_TEST(test_panel_keys_do_not_live_on_settings);
  RUN_TEST(test_transitionEffect_by_name);
  RUN_TEST(test_clock_enums_names_on_the_wire);
  RUN_TEST(test_validate_bad_enum_value);
  RUN_TEST(test_validate_accepts_valid_payload);
  RUN_TEST(test_validate_brightness_range);
  RUN_TEST(test_validate_saturation_range);
  RUN_TEST(test_validate_volume_range);
  RUN_TEST(test_the_old_volume_key_is_rejected);
  RUN_TEST(test_validate_type_mismatch);
  RUN_TEST(test_validate_unknown_key_rejected);
  RUN_TEST(test_validate_bad_transition_name);
  RUN_TEST(test_names_are_case_insensitive);
  RUN_TEST(test_validate_timeMode_range);
  RUN_TEST(test_applyJson_clamps_out_of_range_on_load);
  RUN_TEST(test_validate_bad_color);
  RUN_TEST(test_validate_null_only_on_nullable_colors);
  RUN_TEST(test_canonicalKey_resolves_to_the_table_literal);
  RUN_TEST(test_canonicalKey_rejects_what_is_not_a_flat_field);
  RUN_TEST(test_read_answers_every_field_kind);
  RUN_TEST(test_read_has_no_value_for_unset_or_unknown);
  RUN_TEST(test_read_follows_an_applied_patch);
  RUN_TEST(test_json_roundtrip_covers_every_field);
  return UNITY_END();
}
