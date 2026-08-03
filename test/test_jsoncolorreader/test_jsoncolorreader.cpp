#include <unity.h>

#include <string>

#include "core/JsonColor.h"
#include "core/api/JsonReader.h"

using namespace awtrix;

void setUp() {}
void tearDown() {}

static std::string viaReader(const char* json) {
  api::JsonReader r{std::string_view(json)};
  uint32_t out = 0xDEADBEu;
  const bool ok = color::readColor(r, out);
  char buf[32];
  snprintf(buf, sizeof(buf), "%s:%06X", ok ? "ok" : "no", ok ? (out & 0xFFFFFFu) : 0u);
  return buf;
}

static void same(const char* json, const char* expect) {
  TEST_ASSERT_EQUAL_STRING_MESSAGE(expect, viaReader(json).c_str(), json);
}

static void test_hex_strings() {
  same("\"#FF0000\"", "ok:FF0000");
  same("\"#00ff00\"", "ok:00FF00");
  same("\"#123\"", "ok:112233");
  same("\"FF0000\"", "ok:FF0000");
  same("\"\"", "no:000000");
  same("\"not a colour\"", "no:000000");
}

static void test_rgb_arrays() {
  same("[255,0,0]", "ok:FF0000");
  same("[0,255,0,128]", "ok:00FF00");
  same("[1,2]", "no:000000");
  same("[]", "no:000000");
  same("[\"a\",2,3]", "no:000000");
  same("[1.5,2,3]", "no:000000");
}

static void test_hsv_arrays() {
  same("[\"HSV\",0,255,255]", "ok:FF0000");
  same("[\"HSV\",120,255,128]", "ok:00FF00");
  same("[\"HSV\",1,2]", "no:000000");
  same("[\"hsv\",0,255,255]", "no:000000");
  same("[\"HSV\",\"x\",255,255]", "no:000000");
}

static void test_plain_numbers() {
  same("16711680", "ok:FF0000");
  same("0", "ok:000000");
  same("-1", "ok:000000");
  same("16777216", "ok:000000");
  same("1.5", "no:000000");
}

static void test_rejected_kinds() {
  same("true", "no:000000");
  same("false", "no:000000");
  same("null", "no:000000");
  same("{\"r\":255}", "no:000000");
}

int main(int, char**) {
  UNITY_BEGIN();
  RUN_TEST(test_hex_strings);
  RUN_TEST(test_rgb_arrays);
  RUN_TEST(test_hsv_arrays);
  RUN_TEST(test_plain_numbers);
  RUN_TEST(test_rejected_kinds);
  return UNITY_END();
}
