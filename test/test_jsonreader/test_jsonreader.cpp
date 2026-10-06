#include <unity.h>

#include <cmath>
#include <string>

#include "core/api/JsonReader.h"
#include "core/api/JsonCoerce.h"

using namespace awtrix;
using api::JsonReader;

void setUp() {}
void tearDown() {}

static void test_unsigned_integer_boundaries_and_refusals() {
  for (const auto& text : {"0", "42", "18446744073709551615"}) {
    uint64_t value = 17;
    TEST_ASSERT_TRUE(JsonReader(text).asUnsigned(value));
    TEST_ASSERT_EQUAL_STRING(text, std::to_string(value).c_str());
  }
  for (const auto& text : {"", "null", "true", "\"42\"", "-1", "-0", "+1", "01", "1.0",
                           "1e0", "18446744073709551616", "999999999999999999999", "1x"}) {
    uint64_t value = 17;
    TEST_ASSERT_FALSE_MESSAGE(JsonReader(text).asUnsigned(value), text);
    TEST_ASSERT_EQUAL_UINT64(17, value);
  }
  uint64_t value = 0;
  TEST_ASSERT_TRUE(api::memberValue(JsonReader("{\"counter\":18446744073709551615}"), "counter").asUnsigned(value));
  TEST_ASSERT_EQUAL_UINT64(UINT64_MAX, value);
}

static void test_member_helpers_preserve_limits_and_truthiness() {
  const std::string json = R"({"label":"a\u00e4","empty":"","zero":0,"number":2,"false":false,"null":null,"list":[],"object":{}})";
  const JsonReader reader(json);
  TEST_ASSERT_EQUAL_STRING("a\xc3\xa4", api::memberText(reader, "label", 3).c_str());
  TEST_ASSERT_TRUE(api::memberText(json, "label", 2).empty());
  TEST_ASSERT_TRUE(api::memberText(json, "number").empty());
  TEST_ASSERT_TRUE(api::memberText(json, "missing").empty());
  TEST_ASSERT_TRUE(api::memberText(R"({"label":"bad\uZZZZ"})", "label").empty());
  for (const char* key : {"number", "empty", "list", "object"}) TEST_ASSERT_TRUE(api::memberFlag(reader, key));
  for (const char* key : {"zero", "false", "null", "missing"}) TEST_ASSERT_FALSE(api::memberFlag(json, key));
}

static std::string walk(const char* json) {
  JsonReader r{std::string_view(json)};
  std::string out;
  if (!r.enterObject()) return "!enter";
  while (r.nextMember()) {
    out += std::string(r.key());
    out += '=';
    if (r.isString()) {
      if (!r.appendString(out)) return "!string";
    } else if (r.isNumber()) {
      long long v = 0;
      double d = 0.0;
      if (r.isInteger() && r.asLong(v)) {
        out += std::to_string(v);
      } else if (r.asDouble(d)) {
        char buf[32];
        snprintf(buf, sizeof(buf), "%.4g", d);
        out += buf;
      } else {
        return "!number";
      }
    } else if (r.isBool()) {
      bool b = false;
      r.asBool(b);
      out += b ? "true" : "false";
    } else if (r.isNull()) {
      out += "null";
    } else {
      out += r.isArray() ? "[...]" : "{...}";
    }
    out += ';';
    if (!r.skipValue()) return "!skip";
  }
  return r.ok() ? out : "!ok";
}

static void test_flat_members() {
  TEST_ASSERT_EQUAL_STRING("text=hi;n=42;f=1.5;b=true;z=null;",
                           walk("{\"text\":\"hi\",\"n\":42,\"f\":1.5,\"b\":true,\"z\":null}").c_str());
}

static void test_whitespace_everywhere() {
  TEST_ASSERT_EQUAL_STRING("a=1;b=x;",
                           walk("  {\n \"a\" : 1 ,\t\"b\"\r:\n \"x\" \n} ").c_str());
}

static void test_negative_and_exponent() {
  TEST_ASSERT_EQUAL_STRING("a=-5;b=-0.25;c=1500;",
                           walk("{\"a\":-5,\"b\":-0.25,\"c\":1.5e3}").c_str());
}

static void test_fractional_reads_as_truncated_integer() {
  JsonReader r{std::string_view("{\"x\":7.9}")};
  TEST_ASSERT_TRUE(r.enterObject());
  TEST_ASSERT_TRUE(r.nextMember());
  TEST_ASSERT_FALSE(r.isInteger());
  long long v = 0;
  TEST_ASSERT_TRUE(r.asLong(v));
  TEST_ASSERT_EQUAL_INT64(7, v);
}

static void test_nested_values_are_skipped_whole() {
  TEST_ASSERT_EQUAL_STRING("a=1;deep={...};b=2;",
                           walk("{\"a\":1,\"deep\":{\"x\":[1,2,{\"y\":\"z\"}],\"w\":null},\"b\":2}")
                               .c_str());
}

static void test_escapes_decode() {
  TEST_ASSERT_EQUAL_STRING("s=a\"b\\c\nd\te;",
                           walk("{\"s\":\"a\\\"b\\\\c\\nd\\te\"}").c_str());
}

static void test_unicode_escape_becomes_utf8() {
  const std::string got = walk("{\"s\":\"\\u00e4\\u20ac\"}");
  TEST_ASSERT_EQUAL_STRING("s=\xC3\xA4\xE2\x82\xAC;", got.c_str());
}

static void test_surrogate_pair_becomes_one_code_point() {
  const std::string got = walk("{\"s\":\"\\ud83d\\ude00\"}");
  TEST_ASSERT_EQUAL_STRING("s=\xF0\x9F\x98\x80;", got.c_str());
}

static void test_array_of_arrays() {
  JsonReader r{std::string_view("{\"draw\":[[\"line\",0,1,2,3,\"#F0F\"],[\"pixel\",4,5]]}")};
  TEST_ASSERT_TRUE(r.enterObject());
  TEST_ASSERT_TRUE(r.nextMember());
  TEST_ASSERT_TRUE(r.keyEquals("draw"));
  TEST_ASSERT_TRUE(r.enterArray());
  int commands = 0, args = 0;
  while (r.nextElement()) {
    ++commands;
    TEST_ASSERT_TRUE(r.enterArray());
    while (r.nextElement()) {
      ++args;
      TEST_ASSERT_TRUE(r.skipValue());
    }
  }
  TEST_ASSERT_EQUAL_INT(2, commands);
  TEST_ASSERT_EQUAL_INT(9, args);
  TEST_ASSERT_TRUE(r.ok());
}

static void test_raw_string_is_a_view_until_escapes_appear() {
  JsonReader plain{std::string_view("{\"k\":\"value\"}")};
  plain.enterObject();
  plain.nextMember();
  TEST_ASSERT_EQUAL_STRING("value", std::string(plain.rawString()).c_str());

  JsonReader escaped{std::string_view("{\"k\":\"va\\nlue\"}")};
  escaped.enterObject();
  escaped.nextMember();
  TEST_ASSERT_TRUE(escaped.rawString().empty());
  std::string decoded;
  TEST_ASSERT_TRUE(escaped.appendString(decoded));
  TEST_ASSERT_EQUAL_STRING("va\nlue", decoded.c_str());
}

static void test_empty_containers() {
  TEST_ASSERT_EQUAL_STRING("a=[...];b={...};", walk("{\"a\":[],\"b\":{}}").c_str());
}

static void test_malformed_is_reported_not_guessed() {
  const char* bad[] = {
      "{\"a\":1,}",
      "{\"a\" 1}",
      "{\"a\":1",
      "{\"a\":\"x}",
      "{a:1}",
      "{\"a\":tru}",
  };
  for (const char* j : bad) {
    const std::string got = walk(j);
    char msg[96];
    snprintf(msg, sizeof(msg), "should have been rejected: %s", j);
    TEST_ASSERT_TRUE_MESSAGE(got.rfind('!', 0) == 0, msg);
  }
}

static void test_runaway_nesting_is_refused() {
  std::string j = "{\"a\":";
  for (int i = 0; i < 40; ++i) j += "[";
  for (int i = 0; i < 40; ++i) j += "]";
  j += "}";
  JsonReader r{std::string_view(j)};
  TEST_ASSERT_TRUE(r.enterObject());
  TEST_ASSERT_TRUE(r.nextMember());
  r.skipValue();
  TEST_ASSERT_FALSE(r.ok());
}

static bool sameDouble(double a, double b, double relativeTolerance) {
  if (std::isinf(a) || std::isinf(b)) return a == b;
  if (a == b) return true;
  const double scale = std::fabs(b) > 0.0 ? std::fabs(b) : 1.0;
  return std::fabs(a - b) <= relativeTolerance * scale;
}

static void test_double_parsing_matches_strtod() {
  struct Case { const char* text; double expect; double tolerance; };
  const Case cases[] = {
      {"0.1", 0.1, 0.0},
      {"-0.1", -0.1, 0.0},
      {"1e-7", 1e-7, 0.0},
      {"1E+7", 1e7, 0.0},
      {"42", 42.0, 0.0},
      {"3.141592653589793", 3.141592653589793, 0.0},
      {"12345678901234567890", 12345678901234567890.0, 0.0},
      {"1e22", 1e22, 0.0},
      {"1e308", 1e308, 1e-13},
      {"2.2250738585072014e-308", 2.2250738585072014e-308, 1e-13},
      {"1e999", HUGE_VAL, 0.0},
      {"1e-999", 0.0, 0.0},
      {"0", 0.0, 0.0},
      {"-0", 0.0, 0.0},
  };
  for (const Case& c : cases) {
    double out = 1234.0;
    const std::string_view text(c.text);
    TEST_ASSERT_TRUE_MESSAGE(api::parseDouble(text.data(), text.data() + text.size(), out),
                             c.text);
    TEST_ASSERT_TRUE_MESSAGE(sameDouble(out, c.expect, c.tolerance), c.text);
  }
}

static void test_double_parsing_rejects_the_rest() {
  const char* bad[] = {"", "-", "+", ".", "abc", "1.2.3", "1e", "1e+", "12x", " 1", "1 ",
                       "inf", "nan"};
  for (const char* text : bad) {
    double out = 1234.0;
    const std::string_view v(text);
    TEST_ASSERT_FALSE_MESSAGE(api::parseDouble(v.data(), v.data() + v.size(), out), text);
    TEST_ASSERT_TRUE_MESSAGE(out == 1234.0, text);
  }
}

static void test_raw_control_bytes_are_rejected_but_escaped_controls_remain_valid() {
  for (int value = 0; value < 0x20; ++value) {
    const std::string raw = std::string("\"before") + static_cast<char>(value) + "after\"";
    TEST_ASSERT_FALSE(api::isWellFormed(raw));
    TEST_ASSERT_FALSE(api::isWellFormed("{\"text\":" + raw + "}"));
    TEST_ASSERT_FALSE(api::isWellFormed("{" + raw + ":1}"));
    const JsonReader reader{std::string_view(raw)};
    std::string_view view;
    std::string decoded;
    TEST_ASSERT_TRUE(reader.rawString().empty());
    TEST_ASSERT_FALSE(reader.stringView(view));
    TEST_ASSERT_FALSE(reader.appendString(decoded));
    const std::string slashRaw = std::string("\"\\") + static_cast<char>(value) + "\"";
    TEST_ASSERT_FALSE(api::isWellFormed(slashRaw));
    char escaped[10];
    snprintf(escaped, sizeof(escaped), "\"\\u%04x\"", value);
    TEST_ASSERT_TRUE(api::isWellFormed(escaped));
    decoded.clear();
    TEST_ASSERT_TRUE(JsonReader{std::string_view(escaped)}.appendString(decoded));
    TEST_ASSERT_EQUAL_size_t(1, decoded.size());
    TEST_ASSERT_EQUAL_UINT8(value, static_cast<unsigned char>(decoded[0]));
  }
}

static void test_well_formed_requires_valid_string_escapes_and_unicode_hex_digits() {
  const char* invalid[] = {"\"\\q\"", "\"\\x41\"", "\"\\v\"", "\"\\0\"", "\"\\u\"",
                           "\"\\u123\"", "\"\\u12XZ\"", "\"tail\\", "\"\\'\""};
  for (const char* text : invalid) {
    TEST_ASSERT_FALSE_MESSAGE(api::isWellFormed(text), text);
    TEST_ASSERT_FALSE_MESSAGE(api::isWellFormed(std::string("{\"text\":") + text + "}"), text);
    TEST_ASSERT_FALSE_MESSAGE(api::isWellFormed(std::string("{") + text + ":1}"), text);
  }
  const char* valid[] = {"\"\"", "\"plain\"", "\"\\\"\\\\\\/\\b\\f\\n\\r\\t\"",
                         "\"\\u1234\"", "\"\\uABcd\"", "\"\\uD83D\\uDE00\""};
  for (const char* text : valid) TEST_ASSERT_TRUE_MESSAGE(api::isWellFormed(text), text);
}

static void test_well_formed_requires_json_number_grammar() {
  const char* invalid[] = {"+1", "-", "+", ".", "e", "1.", ".1", "01", "-01", "1e",
                           "1E+", "1e-", "--1", "1-2", "1+2", "0x12", "1.2.3"};
  for (const char* text : invalid) {
    TEST_ASSERT_FALSE_MESSAGE(api::isWellFormed(text), text);
    TEST_ASSERT_FALSE_MESSAGE(api::isWellFormed(std::string("{\"n\":") + text + "}"), text);
    TEST_ASSERT_FALSE_MESSAGE(api::isWellFormed(std::string("[") + text + "]"), text);
  }
  const char* valid[] = {"0", "-0", "12", "-12", "0.2", "-0.2", "1e3", "1e+3", "1e-3",
                         "1.2E3", "1.2E+3", "1.2E-3", "1e99999999", "99999999999999999999999"};
  for (const char* text : valid) TEST_ASSERT_TRUE_MESSAGE(api::isWellFormed(text), text);
}

static void test_truncated_arrays_are_not_complete_json_documents() {
  for (const std::string full : {"[1,2]", "[[1],null,\"value\"]", "{\"array\":[1]}"}) {
    TEST_ASSERT_TRUE(api::isWellFormed(full));
    for (size_t size = 0; size < full.size(); ++size)
      TEST_ASSERT_FALSE_MESSAGE(api::isWellFormed(std::string_view(full.data(), size)),
                                full.substr(0, size).c_str());
  }
}

static void test_empty_object_ignores_whitespace_only() {
  for (const char* text : {"", " ", "{}", "{ }", " {\n\t} ", "{\r\n}"})
    TEST_ASSERT_TRUE_MESSAGE(api::isEmptyObject(text), text);
  for (const char* text : {"{\"a\":1}", "{ } x", "{}}", "{", "[]", "null", "0", "\"\""})
    TEST_ASSERT_FALSE_MESSAGE(api::isEmptyObject(text), text);
}

int main(int, char**) {
  UNITY_BEGIN();
  RUN_TEST(test_unsigned_integer_boundaries_and_refusals);
  RUN_TEST(test_member_helpers_preserve_limits_and_truthiness);
  RUN_TEST(test_double_parsing_matches_strtod);
  RUN_TEST(test_double_parsing_rejects_the_rest);
  RUN_TEST(test_raw_control_bytes_are_rejected_but_escaped_controls_remain_valid);
  RUN_TEST(test_well_formed_requires_valid_string_escapes_and_unicode_hex_digits);
  RUN_TEST(test_well_formed_requires_json_number_grammar);
  RUN_TEST(test_truncated_arrays_are_not_complete_json_documents);
  RUN_TEST(test_flat_members);
  RUN_TEST(test_whitespace_everywhere);
  RUN_TEST(test_negative_and_exponent);
  RUN_TEST(test_fractional_reads_as_truncated_integer);
  RUN_TEST(test_nested_values_are_skipped_whole);
  RUN_TEST(test_escapes_decode);
  RUN_TEST(test_unicode_escape_becomes_utf8);
  RUN_TEST(test_surrogate_pair_becomes_one_code_point);
  RUN_TEST(test_array_of_arrays);
  RUN_TEST(test_raw_string_is_a_view_until_escapes_appear);
  RUN_TEST(test_empty_containers);
  RUN_TEST(test_empty_object_ignores_whitespace_only);
  RUN_TEST(test_malformed_is_reported_not_guessed);
  RUN_TEST(test_runaway_nesting_is_refused);
  return UNITY_END();
}
