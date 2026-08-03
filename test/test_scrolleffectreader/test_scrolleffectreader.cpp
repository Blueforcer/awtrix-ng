#include <unity.h>

#include <string>

#include "core/api/JsonReader.h"
#include "core/payload/EffectSettingsJson.h"
#include "core/payload/ScrollSpec.h"

using namespace awtrix;

void setUp() {}
void tearDown() {}

static std::string dumpScroll(bool ok, const ScrollSpec& s, const scroll::Error& e) {
  std::string d = ok ? "ok" : "no";
  d += "|" + e.field + "|" + e.message + "|";
  auto f = [&](bool has, int v) { d += (has ? "1:" : "0:") + std::to_string(v) + ","; };
  f(s.hasMode, static_cast<int>(s.mode));
  f(s.hasDirection, static_cast<int>(s.direction));
  f(s.hasEntry, static_cast<int>(s.entry));
  f(s.hasWhenFits, static_cast<int>(s.whenFits));
  f(s.hasSpeed, s.speed);
  f(s.hasGap, s.gap);
  return d;
}

static std::string scrollViaReader(const char* json) {
  api::JsonReader r{std::string_view(json)};
  ScrollSpec s;
  scroll::Error e;
  const bool ok = scroll::read(r, s, e);
  return dumpScroll(ok, s, e);
}

static void sameScroll(const char* json, const char* expect) {
  TEST_ASSERT_EQUAL_STRING_MESSAGE(expect, scrollViaReader(json).c_str(), json);
}

static void test_scroll_shorthand_and_null() {
  sameScroll("null", "ok|||0:1,0:0,0:0,0:0,0:100,0:8,");
  sameScroll("\"bounce\"", "ok|||1:3,0:0,0:0,0:0,0:100,0:8,");
  sameScroll("\"static\"", "ok|||1:0,0:0,0:0,0:0,0:100,0:8,");
  sameScroll("\"sideways\"", "no|scroll|unknown value|0:1,0:0,0:0,0:0,0:100,0:8,");
  sameScroll("42", "no|scroll|must be an object or a mode string|0:1,0:0,0:0,0:0,0:100,0:8,");
  sameScroll("true", "no|scroll|must be an object or a mode string|0:1,0:0,0:0,0:0,0:100,0:8,");
  sameScroll("[1,2]", "no|scroll|must be an object or a mode string|0:1,0:0,0:0,0:0,0:100,0:8,");
}

static void test_scroll_object() {
  sameScroll("{}", "ok|||0:1,0:0,0:0,0:0,0:100,0:8,");
  sameScroll("{\"mode\":\"loop\",\"direction\":\"right\",\"entry\":\"offscreen\","
             "\"whenFits\":\"scroll\",\"speed\":150,\"gap\":4}",
             "ok|||1:2,1:1,1:1,1:1,1:150,1:4,");
  sameScroll("{\"speed\":0}", "ok|||0:1,0:0,0:0,0:0,1:0,0:8,");
}

static void test_scroll_rejects() {
  sameScroll("{\"mode\":\"diagonal\"}", "no|scroll.mode|unknown value|0:1,0:0,0:0,0:0,0:100,0:8,");
  sameScroll("{\"mode\":7}", "no|scroll.mode|unknown value|0:1,0:0,0:0,0:0,0:100,0:8,");
  sameScroll("{\"speed\":-1}", "no|scroll.speed|must be a non-negative integer|0:1,0:0,0:0,0:0,0:100,0:8,");
  sameScroll("{\"speed\":true}", "no|scroll.speed|must be a non-negative integer|0:1,0:0,0:0,0:0,0:100,0:8,");
  sameScroll("{\"speed\":1.5}", "no|scroll.speed|must be a non-negative integer|0:1,0:0,0:0,0:0,0:100,0:8,");
  sameScroll("{\"speed\":\"fast\"}", "no|scroll.speed|must be a non-negative integer|0:1,0:0,0:0,0:0,0:100,0:8,");
  sameScroll("{\"nope\":1}", "no|scroll.nope|unknown field|0:1,0:0,0:0,0:0,0:100,0:8,");
  sameScroll("{\"speed\":100,\"nope\":1}", "no|scroll.nope|unknown field|0:1,0:0,0:0,0:0,1:100,0:8,");
}

static std::string dumpFx(bool ok, const EffectSettings& s) {
  char buf[64];
  snprintf(buf, sizeof(buf), "%d|%d:%.4f|%d:%06X,%06X|%d", ok ? 1 : 0, s.hasSpeed ? 1 : 0,
           static_cast<double>(s.speed), s.ramp.valid() ? 1 : 0,
           s.ramp.valid() ? (s.ramp.palette().entries[0] & 0xFFFFFFu) : 0u,
           s.ramp.valid() ? (s.ramp.palette().entries[15] & 0xFFFFFFu) : 0u, s.ramp.blend ? 1 : 0);
  return buf;
}

static std::string fxViaReader(const char* json) {
  api::JsonReader r{std::string_view(json)};
  EffectSettings s;
  const bool ok = payload::readEffectSettings(r, s);
  return dumpFx(ok, s);
}

static void sameFx(const char* json, const char* expect) {
  TEST_ASSERT_EQUAL_STRING_MESSAGE(expect, fxViaReader(json).c_str(), json);
}

static void test_effect_speed() {
  sameFx("{}", "1|0:1.0000|0:000000,000000|1");
  sameFx("{\"speed\":2.5}", "1|1:2.5000|0:000000,000000|1");
  sameFx("{\"speed\":0}", "1|1:0.1000|0:000000,000000|1");
  sameFx("{\"speed\":-3}", "1|1:0.1000|0:000000,000000|1");
  sameFx("{\"speed\":99}", "1|1:10.0000|0:000000,000000|1");
  sameFx("{\"speed\":true}", "1|1:1.0000|0:000000,000000|1");
  sameFx("{\"speed\":null}", "1|1:0.1000|0:000000,000000|1");
}

static void test_effect_palette() {
  sameFx("{\"palette\":[\"#FF0000\",\"#00FF00\"]}", "1|0:1.0000|1:FF0000,00FF00|1");
  sameFx("{\"palette\":[1,2,3]}", "1|0:1.0000|1:000001,000003|1");
  sameFx("{\"palette\":\"Rainbow\"}", "1|0:1.0000|1:FF0000,D5002B|1");
  sameFx("{\"palette\":null}", "1|0:1.0000|0:000000,000000|1");
  sameFx("{\"palette\":[]}", "0|0:1.0000|0:000000,000000|1");
  sameFx("{\"palette\":\"nosuchpalette\"}", "0|0:1.0000|0:000000,000000|1");
  sameFx("{\"palette\":42}", "0|0:1.0000|0:000000,000000|1");
}

static void test_effect_blend() {
  sameFx("{\"blend\":false}", "1|0:1.0000|0:000000,000000|0");
  sameFx("{\"palette\":[\"#FF0000\"],\"blend\":true}", "1|0:1.0000|1:FF0000,FF0000|1");
  sameFx("{\"blend\":false,\"palette\":[\"#FF0000\"]}", "1|0:1.0000|1:FF0000,FF0000|0");
  sameFx("{\"palette\":[\"#FF0000\"],\"blend\":0}", "1|0:1.0000|1:FF0000,FF0000|0");
}

int main(int, char**) {
  UNITY_BEGIN();
  RUN_TEST(test_scroll_shorthand_and_null);
  RUN_TEST(test_scroll_object);
  RUN_TEST(test_scroll_rejects);
  RUN_TEST(test_effect_speed);
  RUN_TEST(test_effect_palette);
  RUN_TEST(test_effect_blend);
  return UNITY_END();
}
