#include <unity.h>

#include <string>

#include "core/payload/PayloadParser.h"

using namespace awtrix;

void setUp() {}
void tearDown() {}

static void put(std::string& d, const char* k, const std::string& v) {
  d += k;
  d += '=';
  d += v;
  d += ';';
}
static void put(std::string& d, const char* k, long long v) { put(d, k, std::to_string(v)); }
static void putB(std::string& d, const char* k, bool v) { put(d, k, std::string(v ? "1" : "0")); }
static std::string hex(uint32_t v) {
  char b[12];
  snprintf(b, sizeof(b), "%06X", v & 0xFFFFFFu);
  return b;
}

static std::string dump(const AppSpec& s) {
  std::string d;
  put(d, "name", s.name);
  putB(d, "notif", s.isNotification);
  put(d, "text", s.text);
  d += "frags=";
  for (const TextFragment& f : s.fragments) d += f.text + "/" + hex(f.color) + ",";
  d += ';';
  put(d, "case", static_cast<long long>(s.textCase));
  putB(d, "inFront", s.textInFront);
  putB(d, "center", s.textCenter);
  putB(d, "hasCol", s.hasTextColor);
  put(d, "col", hex(s.textColor));
  put(d, "blink", s.textBlinkMs);
  put(d, "fade", s.textFadeMs);
  putB(d, "hasBg", s.hasBackgroundColor);
  put(d, "bg", hex(s.backgroundColor));
  put(d, "icon", s.icon);
  put(d, "iconMode", static_cast<long long>(s.iconMode));
  put(d, "iconX", s.iconOffsetX);
  put(d, "textX", s.textOffsetX);
  put(d, "repeat", s.repeat);
  put(d, "durMs", s.durationMs);
  put(d, "lifeMs", s.lifetimeMs);
  put(d, "expiry", static_cast<long long>(s.lifetimeExpiry));
  put(d, "effect", s.effect);
  put(d, "overlay", s.overlay);
  putB(d, "hold", s.hold);
  putB(d, "stack", s.stack);
  putB(d, "wake", s.wakeup);
  put(d, "sound", s.sound);
  putB(d, "loopSnd", s.loopSound);

  const AppSpecExtras& x = s.extras();
  putB(d, "pal", x.palette.valid());
  if (x.palette.valid())
    put(d, "palEnds", hex(x.palette.palette().entries[0]) + "/" + hex(x.palette.palette().entries[15]));
  putB(d, "palBlend", x.palette.blend);
  put(d, "palSpan", static_cast<long long>(x.palette.spanPx));
  putB(d, "textPal", x.textUsesPalette);
  putB(d, "chartPal", x.chartUsesPalette);
  putB(d, "progPal", x.progressUsesPalette);
  d += "bar=";
  for (int v : x.barChart) d += std::to_string(v) + ",";
  d += ";line=";
  for (int v : x.lineChart) d += std::to_string(v) + ",";
  d += ';';
  putB(d, "autoscale", x.chartAutoscale);
  putB(d, "hasChartCol", x.hasChartColor);
  put(d, "chartCol", hex(x.chartColor));
  put(d, "prog", x.progress);
  put(d, "progCol", hex(x.progressColor));
  put(d, "progTrack", hex(x.progressTrackColor));
  putB(d, "hasFxSpeed", x.hasEffectSpeed);
  put(d, "rtttl", x.rtttl);
  d += "draw=";
  for (const DrawOp& op : x.draw) {
    d += std::to_string(static_cast<int>(op.kind));
    d += ":" + std::to_string(op.x) + "," + std::to_string(op.y) + "," + std::to_string(op.x2) +
         "," + std::to_string(op.y2) + "," + std::to_string(op.w) + "," + std::to_string(op.h) +
         "," + std::to_string(op.r) + "/" + hex(op.color) + (op.inheritColor ? "i" : "") + "/" +
         op.text + "/" + std::to_string(op.bitmap.size()) + "/" + std::to_string(op.points.size());
    d += "|";
  }
  d += ';';
  return d;
}

static std::string run(const char* json, bool isNotif = false) {
  AppSpec s;
  DispatchDetail err;
  const bool ok = payload::parse(json, isNotif, s, nullptr, nullptr, &err);
  return std::string(ok ? "ok" : "no") + "|" + err.field + "|" + dump(s);
}

static void check(const char* json, const char* expected, bool isNotif = false) {
  const std::string got = run(json, isNotif);
  char msg[256];
  snprintf(msg, sizeof(msg), "payload %s", json);
  TEST_ASSERT_EQUAL_STRING_MESSAGE(expected, got.c_str(), msg);
}

static const char* kDefaults =
    "ok||name=;notif=0;text=;frags=;case=0;inFront=0;center=1;hasCol=0;col=FFFFFF;"
    "blink=0;fade=0;hasBg=0;bg=000000;icon=;iconMode=0;iconX=0;textX=0;repeat=0;durMs=0;"
    "lifeMs=0;expiry=0;effect=;overlay=;hold=0;stack=1;wake=0;sound=;loopSnd=0;pal=0;"
    "palBlend=1;palSpan=0;textPal=0;chartPal=0;progPal=0;bar=;"
    "line=;autoscale=1;hasChartCol=0;chartCol=000000;prog=-1;progCol=00FF00;progTrack=FFFFFF;"
    "hasFxSpeed=0;rtttl=;draw=;";

static void test_defaults() { check("{}", kDefaults); }

static void test_wrong_types_fall_back_to_defaults() {
  TEST_ASSERT_EQUAL_STRING(kDefaults, run("{\"textCenter\":\"yes\"}").c_str());
  TEST_ASSERT_EQUAL_STRING(kDefaults, run("{\"textInFront\":1}").c_str());
  TEST_ASSERT_EQUAL_STRING(kDefaults, run("{\"textBlinkMs\":\"soon\"}").c_str());
  TEST_ASSERT_EQUAL_STRING(kDefaults, run("{\"repeat\":\"lots\"}").c_str());
  TEST_ASSERT_EQUAL_STRING(kDefaults, run("{\"lifetimeMs\":true}").c_str());
  TEST_ASSERT_EQUAL_STRING(kDefaults, run("{\"text\":42}").c_str());
}

static void test_unknown_key_beats_a_bad_value() {
  const std::string got = run("{\"textColor\":\"nonsense\",\"mystery\":1}");
  TEST_ASSERT_TRUE(got.rfind("no|mystery|", 0) == 0);
}

static void test_unknown_key_alone() {
  const std::string got = run("{\"mystery\":1}");
  TEST_ASSERT_TRUE(got.rfind("no|mystery|", 0) == 0);
}

static void test_notification_keys_are_gated() {
  TEST_ASSERT_TRUE(run("{\"hold\":true}").rfind("no|hold|", 0) == 0);
  TEST_ASSERT_TRUE(run("{\"hold\":true}", true).rfind("ok||", 0) == 0);
}

static void test_scalar_fields() {
  check("{\"text\":\"hi\",\"textCase\":\"upper\",\"textInFront\":true,\"textCenter\":false,"
        "\"textBlinkMs\":250,\"textFadeMs\":100,\"textOffsetX\":3,"
        "\"icon\":\"clock\",\"iconMode\":\"push\",\"iconOffsetX\":-2,\"repeat\":4,"
        "\"durationMs\":5000,\"lifetimeMs\":60000,\"lifetimeExpiry\":\"mark\","
        "\"effect\":\"Rain\",\"overlay\":\"SNOW\"}",
        "ok||name=;notif=0;text=hi;frags=;case=1;inFront=1;center=0;hasCol=0;col=FFFFFF;"
        "blink=250;fade=100;hasBg=0;bg=000000;icon=clock;iconMode=2;iconX=-2;textX=3;repeat=4;"
        "durMs=5000;lifeMs=60000;expiry=1;effect=Rain;overlay=snow;hold=0;stack=1;wake=0;sound=;"
        "loopSnd=0;pal=0;palBlend=1;palSpan=0;textPal=0;chartPal=0;progPal=0;bar=;line=;"
        "autoscale=1;hasChartCol=0;chartCol=000000;prog=-1;"
        "progCol=00FF00;progTrack=FFFFFF;hasFxSpeed=0;rtttl=;draw=;");
}

static void test_colors_and_palette() {
  const std::string got = run("{\"textColor\":\"#FF0000\",\"backgroundColor\":[0,255,0],"
                              "\"palette\":[\"#000080\",16711935]}");
  TEST_ASSERT_TRUE_MESSAGE(got.find("hasCol=1;col=FF0000;") != std::string::npos, got.c_str());
  TEST_ASSERT_TRUE_MESSAGE(got.find("hasBg=1;bg=00FF00;") != std::string::npos, got.c_str());
  TEST_ASSERT_TRUE_MESSAGE(got.find("pal=1;palEnds=000080/FF00FF;") != std::string::npos,
                           got.c_str());
}

static void test_fragments() {
  const std::string got =
      run("{\"text\":[{\"text\":\"a\",\"color\":\"#FF0000\"},{\"text\":\"b\"}]}");
  TEST_ASSERT_TRUE_MESSAGE(got.find("frags=a/FF0000,b/FFFFFF,;") != std::string::npos, got.c_str());
}

static void test_charts_are_capped_at_sixteen() {
  std::string j = "{\"barChart\":[";
  for (int i = 0; i < 20; ++i) j += (i ? "," : "") + std::to_string(i);
  j += "],\"lineChart\":[9,8,7],\"chartAutoscale\":false,\"chartColor\":\"#123456\"}";
  const std::string got = run(j.c_str());
  TEST_ASSERT_TRUE_MESSAGE(
      got.find("bar=0,1,2,3,4,5,6,7,8,9,10,11,12,13,14,15,;line=9,8,7,;autoscale=0;"
               "hasChartCol=1;chartCol=123456;") != std::string::npos,
      got.c_str());
}

static void test_progress() {
  const std::string got =
      run("{\"progress\":42,\"progressColor\":\"#111111\",\"progressTrackColor\":\"#222222\"}");
  TEST_ASSERT_TRUE_MESSAGE(got.find("prog=42;progCol=111111;progTrack=222222;") != std::string::npos,
                           got.c_str());
}

static void test_draw_commands() {
  const std::string got = run("{\"draw\":[[\"line\",0,1,2,3,\"#FF0000\"],[\"pixel\",4,5],"
                              "[\"rectFill\",1,2,3,4],[\"circle\",5,5,2,\"#00FF00\"],"
                              "[\"text\",1,2,\"hi\",\"#0000FF\"]]}");
  TEST_ASSERT_TRUE_MESSAGE(got.rfind("ok||", 0) == 0, got.c_str());
  const std::size_t at = got.find("draw=");
  TEST_ASSERT_TRUE(at != std::string::npos);
  TEST_ASSERT_EQUAL_STRING(
      "draw=2:0,1,2,3,0,0,0/FF0000//0/0|0:4,5,0,0,0,0,0/FFFFFFi//0/0|"
      "4:1,2,0,0,3,4,0/FFFFFFi//0/0|5:5,5,0,0,0,0,2/00FF00//0/0|7:1,2,0,0,0,0,0/0000FF/hi/0/0|;",
      got.substr(at).c_str());
}

static void test_draw_pixels_and_bitmap() {
  const std::string got =
      run("{\"draw\":[[\"pixels\",\"#FF0000\",0,0,1,1,2,2],[\"bitmap\",0,0,2,1,[1,2]]]}");
  TEST_ASSERT_TRUE_MESSAGE(got.rfind("ok||", 0) == 0, got.c_str());
  TEST_ASSERT_TRUE_MESSAGE(got.find("/0/6|") != std::string::npos, got.c_str());
  TEST_ASSERT_TRUE_MESSAGE(got.find("/2/0|") != std::string::npos, got.c_str());
}

static void test_draw_errors_name_their_index() {
  TEST_ASSERT_TRUE(run("{\"draw\":[[\"line\",0,1,2,3],[\"nope\",1]]}").rfind("no|draw[1]|", 0) == 0);
  TEST_ASSERT_TRUE(run("{\"draw\":[[\"line\",0,1]]}").rfind("no|draw[0]|", 0) == 0);
  TEST_ASSERT_TRUE(run("{\"draw\":[[\"line\",\"a\",1,2,3]]}").rfind("no|draw[0]|", 0) == 0);
}

static void test_enum_errors_name_the_field() {
  TEST_ASSERT_TRUE(run("{\"textCase\":\"sideways\"}").rfind("no|textCase|", 0) == 0);
  TEST_ASSERT_TRUE(run("{\"iconMode\":\"shove\"}").rfind("no|iconMode|", 0) == 0);
  TEST_ASSERT_TRUE(run("{\"lifetimeExpiry\":\"vanish\"}").rfind("no|lifetimeExpiry|", 0) == 0);
}

static void test_notification_extras() {
  const std::string got = run("{\"name\":\"alarm\",\"hold\":true,\"stack\":false,\"wakeup\":true,"
                              "\"sound\":7,\"soundLoop\":true,\"soundRtttl\":\"a:d=4,o=5,b=120:c\"}",
                              true);
  TEST_ASSERT_TRUE_MESSAGE(got.find("name=alarm;notif=1;") != std::string::npos, got.c_str());
  TEST_ASSERT_TRUE_MESSAGE(got.find("hold=1;stack=0;wake=1;sound=7;loopSnd=1;") != std::string::npos,
                           got.c_str());
  TEST_ASSERT_TRUE_MESSAGE(got.find("rtttl=a:d=4,o=5,b=120:c;") != std::string::npos,
                           got.c_str());
}

static void test_sound_accepts_string_and_number() {
  TEST_ASSERT_TRUE(run("{\"sound\":\"beep\"}", true).find("sound=beep;") != std::string::npos);
  TEST_ASSERT_TRUE(run("{\"sound\":12}", true).find("sound=12;") != std::string::npos);
  TEST_ASSERT_TRUE(run("{\"sound\":1.5}", true).find("sound=;") != std::string::npos);
}

static void test_utf8_text_is_folded_to_ascii() {
  TEST_ASSERT_TRUE(run("{\"text\":\"Grüße\"}").find("text=Gruesse;") != std::string::npos ||
                   run("{\"text\":\"Grüße\"}").find("text=Gr") != std::string::npos);
}

int main(int, char**) {
  UNITY_BEGIN();
  RUN_TEST(test_defaults);
  RUN_TEST(test_wrong_types_fall_back_to_defaults);
  RUN_TEST(test_unknown_key_beats_a_bad_value);
  RUN_TEST(test_unknown_key_alone);
  RUN_TEST(test_notification_keys_are_gated);
  RUN_TEST(test_scalar_fields);
  RUN_TEST(test_colors_and_palette);
  RUN_TEST(test_fragments);
  RUN_TEST(test_charts_are_capped_at_sixteen);
  RUN_TEST(test_progress);
  RUN_TEST(test_draw_commands);
  RUN_TEST(test_draw_pixels_and_bitmap);
  RUN_TEST(test_draw_errors_name_their_index);
  RUN_TEST(test_enum_errors_name_the_field);
  RUN_TEST(test_notification_extras);
  RUN_TEST(test_sound_accepts_string_and_number);
  RUN_TEST(test_utf8_text_is_folded_to_ascii);
  return UNITY_END();
}
