#include <unity.h>

#include <cmath>
#include <limits>
#include <string>

#include "core/api/JsonWriter.h"

using namespace awtrix;
using api::JsonWriter;

void setUp() {}
void tearDown() {}

static std::string viaWriter(void (*fn)(JsonWriter&)) {
  std::string out;
  JsonWriter w(out);
  fn(w);
  return out;
}

static void assertSame(const std::string& a, const char* expect) {
  TEST_ASSERT_EQUAL_STRING(expect, a.c_str());
}

static void test_flat_scalars_match() {
  assertSame(viaWriter([](JsonWriter& w) {
               w.beginObject();
               w.member("version", "1.0.4-dev");
               w.member("wifiRssi", -58);
               w.member("uptimeSeconds", 1173L);
               w.member("freeHeapBytes", 107412u);
               w.member("matrixPower", true);
               w.member("lowBattery", false);
               w.endObject();
             }),
             "{\"version\":\"1.0.4-dev\",\"wifiRssi\":-58,\"uptimeSeconds\":1173,\"freeHeapBytes\":107412,\"matrixPower\":true,\"lowBattery\":false}");
}

static void test_a_failed_sensor_reading_stays_valid_json() {
  const double nan = std::nan("");
  const double inf = std::numeric_limits<double>::infinity();
  std::string out;
  JsonWriter w(out);
  w.beginObject();
  w.member("temperature", nan, 1);
  w.member("humidity", 25.1, 1);
  w.member("pressureHpa", inf, 1);
  w.member("lightLevel", -inf, 1);
  w.member("batteryVoltage", static_cast<float>(nan));
  w.endObject();
  assertSame(out,
             "{\"temperature\":null,\"humidity\":25.1,\"pressureHpa\":null,\"lightLevel\":null,"
             "\"batteryVoltage\":null}");
}

static void test_rounded_numbers_match() {
  assertSame(viaWriter([](JsonWriter& w) {
               w.beginObject();
               w.member("lightLevel", 0.0, 1);
               w.member("batteryVoltage", 4.17, 2);
               w.member("temperature", 21.5, 1);
               w.member("humidity", 47.0, 1);
               w.member("pressureHpa", 1013.2, 1);
               w.endObject();
             }),
             "{\"lightLevel\":0,\"batteryVoltage\":4.17,\"temperature\":21.5,\"humidity\":47,\"pressureHpa\":1013.2}");
}

static void test_float_values_match() {
  assertSame(viaWriter([](JsonWriter& w) {
               w.beginObject();
               w.member("one", 1.0f);
               w.member("threeEighths", 0.375f);
               w.member("half", 1.5f);
               w.member("tenth", 0.1f);
               w.member("third", 1.0f / 3.0f);
               w.member("big", 1234.5f);
               w.member("negative", -0.25f);
               w.endObject();
             }),
             "{\"one\":1,\"threeEighths\":0.375,\"half\":1.5,\"tenth\":0.100000001,\"third\":0.333333343,\"big\":1234.5,\"negative\":-0.25}");
}

static void test_float_formats_match_across_the_range() {
  struct Case {
    double value;
    const char* expect;
  };
  static const Case kCases[] = {
      {0.0, "0"},
      {1.0, "1"},
      {1.9, "1.9"},
      {2.2, "2.2"},
      {0.1, "0.1"},
      {0.5, "0.5"},
      {1.0 / 3.0, "0.333333333"},
      {2.0 / 3.0, "0.666666667"},
      {1.5, "1.5"},
      {99.99, "99.99"},
      {123.456, "123.456"},
      {1023.4, "1023.4"},
      {65535.0, "65535"},
      {1234567.0, "1234567"},
      {9999999.0, "9999999"},
      {1e7, "1e7"},
      {1.5e7, "1.5e7"},
      {1e8, "1e8"},
      {1.7976931348623157e308, "1.797693135e308"},
      {1e-5, "1e-5"},
      {9.9e-6, "9.9e-6"},
      {1e-6, "1e-6"},
      {1e-9, "1e-9"},
      {0.000012345, "0.000012345"},
      {4.17, "4.17"},
      {21.5, "21.5"},
      {1013.25, "1013.25"},
      {255.0, "255"},
      {-0.25, "-0.25"},
      {-1.9, "-1.9"},
      {-1013.25, "-1013.25"},
      {-1e-7, "-1e-7"},
  };
  for (const Case& c : kCases) {
    std::string out;
    JsonWriter w(out);
    w.value(c.value);
    TEST_ASSERT_EQUAL_STRING_MESSAGE(c.expect, out.c_str(), c.expect);
  }
}

static void test_float_overload_matches_the_document() {
  struct Case {
    float value;
    const char* expect;
  };
  static const Case kCases[] = {
      {1.9f, "1.899999976"},
      {2.2f, "2.200000048"},
      {0.1f, "0.100000001"},
      {1.0f / 3.0f, "0.333333343"},
      {100.0f, "100"},
      {1.25f, "1.25"},
      {99.999f, "99.99900055"},
      {1e-6f, "9.999999975e-7"},
      {1e8f, "1e8"},
      {-1.9f, "-1.899999976"},
  };
  for (const Case& c : kCases) {
    std::string out;
    JsonWriter w(out);
    w.value(c.value);
    TEST_ASSERT_EQUAL_STRING_MESSAGE(c.expect, out.c_str(), c.expect);
  }
}

static void test_nested_object_and_array_match() {
  assertSame(viaWriter([](JsonWriter& w) {
               w.beginObject();
               w.member("power", true);
               w.key("overlay");
               w.null();
               w.key("overlaySettings");
               w.beginObject();
               w.member("speed", 1.0, 1);
               w.key("palette");
               w.beginArray();
               w.value("#FF0000");
               w.value("#00FF00");
               w.endArray();
               w.member("blend", false);
               w.endObject();
               w.key("indicators");
               w.beginArray();
               for (int i = 0; i < 3; ++i) {
                 w.beginObject();
                 w.member("on", i == 1);
                 w.member("blinkMs", i * 100);
                 w.endObject();
               }
               w.endArray();
               w.endObject();
             }),
             "{\"power\":true,\"overlay\":null,\"overlaySettings\":{\"speed\":1,\"palette\":[\"#FF0000\",\"#00FF00\"],\"blend\":false},\"indicators\":[{\"on\":false,\"blinkMs\":0},{\"on\":true,\"blinkMs\":100},{\"on\":false,\"blinkMs\":200}]}");
}

static void test_escaping_matches() {
  assertSame(viaWriter([](JsonWriter& w) {
               w.beginObject();
               w.member("quote", "say \"hi\"");
               w.member("backslash", "a\\b");
               w.member("control", std::string("tab\there\nand\rmore"));
               w.member("utf8", "Grüße");
               w.endObject();
             }),
             "{\"quote\":\"say \\\"hi\\\"\",\"backslash\":\"a\\\\b\",\"control\":\"tab\\there\\nand\\rmore\",\"utf8\":\"Grüße\"}");
}

static void test_empty_containers_match() {
  assertSame(viaWriter([](JsonWriter& w) {
               w.beginObject();
               w.key("nothing");
               w.beginArray();
               w.endArray();
               w.key("blank");
               w.beginObject();
               w.endObject();
               w.endObject();
             }),
             "{\"nothing\":[],\"blank\":{}}");
}

static void test_sixty_four_bit_extremes_match() {
  assertSame(viaWriter([](JsonWriter& w) {
               w.beginObject();
               w.member("min", static_cast<long long>(-9223372036854775807LL - 1));
               w.member("max", static_cast<long long>(9223372036854775807LL));
               w.member("umax", static_cast<unsigned long long>(18446744073709551615ULL));
               w.member("zero", static_cast<long long>(0));
               w.member("uzero", static_cast<unsigned long long>(0));
               w.member("negone", static_cast<long long>(-1));
               w.endObject();
             }),
             "{\"min\":-9223372036854775808,\"max\":9223372036854775807,"
             "\"umax\":18446744073709551615,\"zero\":0,\"uzero\":0,\"negone\":-1}");
}

static void test_raw_int_appenders_cover_the_range() {
  std::string out;
  api::appendInt(out, 0);
  TEST_ASSERT_EQUAL_STRING("0", out.c_str());

  out.clear();
  api::appendInt(out, -1);
  TEST_ASSERT_EQUAL_STRING("-1", out.c_str());

  out.clear();
  api::appendInt(out, 9223372036854775807LL);
  TEST_ASSERT_EQUAL_STRING("9223372036854775807", out.c_str());

  out.clear();
  api::appendInt(out, -9223372036854775807LL - 1);
  TEST_ASSERT_EQUAL_STRING("-9223372036854775808", out.c_str());

  out.clear();
  api::appendUnsigned(out, 18446744073709551615ULL);
  TEST_ASSERT_EQUAL_STRING("18446744073709551615", out.c_str());

  out = "n:";
  api::appendInt(out, 42);
  TEST_ASSERT_EQUAL_STRING("n:42", out.c_str());
}

int main(int, char**) {
  UNITY_BEGIN();
  RUN_TEST(test_raw_int_appenders_cover_the_range);
  RUN_TEST(test_sixty_four_bit_extremes_match);
  RUN_TEST(test_flat_scalars_match);
  RUN_TEST(test_rounded_numbers_match);
  RUN_TEST(test_a_failed_sensor_reading_stays_valid_json);
  RUN_TEST(test_float_values_match);
  RUN_TEST(test_float_formats_match_across_the_range);
  RUN_TEST(test_float_overload_matches_the_document);
  RUN_TEST(test_nested_object_and_array_match);
  RUN_TEST(test_escaping_matches);
  RUN_TEST(test_empty_containers_match);
  return UNITY_END();
}
