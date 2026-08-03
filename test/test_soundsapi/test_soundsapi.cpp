#include <unity.h>

#include <string>
#include <string_view>

#include "core/api/JsonReader.h"
#include "core/api/SoundsApi.h"

using namespace awtrix;
using namespace awtrix::api;

void setUp() {}
void tearDown() {}

static std::string parseJson(const std::string& s) {
  JsonReader probe{std::string_view(s)};
  TEST_ASSERT_TRUE_MESSAGE(probe.skipValue() && probe.atEnd(), ("not JSON: " + s).c_str());
  return s;
}

static std::string strAt(const std::string& json, const char* key) {
  std::string v;
  memberValue(JsonReader(json), key).appendString(v);
  return v;
}
static uint32_t uintAt(const std::string& json, const char* key) {
  long long v = 0;
  memberValue(JsonReader(json), key).asLong(v);
  return static_cast<uint32_t>(v);
}
static bool hasKey(const std::string& json, const char* key) {
  return memberValue(JsonReader(json), key).type() != JsonReader::Type::Invalid;
}
static bool boolAt(const std::string& json, const char* key) {
  bool v = false;
  memberValue(JsonReader(json), key).asBool(v);
  return v;
}

static void test_name_from_file() {
  TEST_ASSERT_EQUAL_STRING("doorbell", sounds::nameFromFile("doorbell.txt").c_str());
  TEST_ASSERT_EQUAL_STRING("doorbell", sounds::nameFromFile("/MELODIES/doorbell.txt").c_str());
  TEST_ASSERT_EQUAL_STRING("a-b_1", sounds::nameFromFile("a-b_1.txt").c_str());
}

static void test_name_from_file_rejects_the_rest() {
  TEST_ASSERT_TRUE(sounds::nameFromFile("doorbell.gif").empty());
  TEST_ASSERT_TRUE(sounds::nameFromFile("doorbell").empty());
  TEST_ASSERT_TRUE(sounds::nameFromFile(".txt").empty());
  TEST_ASSERT_TRUE(sounds::nameFromFile("has space.txt").empty());
  TEST_ASSERT_TRUE(sounds::nameFromFile("").empty());
}

static void test_path_for() {
  TEST_ASSERT_EQUAL_STRING("/MELODIES/doorbell.txt", sounds::pathFor("doorbell").c_str());
}

static void test_entry_json_carries_the_derived_facts() {
  const std::string j =
      sounds::entryJson("doorbell", "doorbell:d=4,o=5,b=100:e,c", 26);
  const std::string d = parseJson(j);
  TEST_ASSERT_EQUAL_STRING("doorbell", strAt(d, "name").c_str());
  TEST_ASSERT_EQUAL_STRING("doorbell:d=4,o=5,b=100:e,c", strAt(d, "rtttl").c_str());
  TEST_ASSERT_EQUAL_UINT32(26u, uintAt(d, "bytes"));
  TEST_ASSERT_EQUAL_UINT32(2u, uintAt(d, "notes"));
  TEST_ASSERT_EQUAL_UINT32(1200u, uintAt(d, "durationMs"));
  TEST_ASSERT_TRUE(boolAt(d, "valid"));
  TEST_ASSERT_FALSE(hasKey(d, "error"));
}

static void test_entry_json_reports_a_broken_file_instead_of_hiding_it() {
  const std::string j = sounds::entryJson("broken", "d=4,o=5,b=120:c,e,g", 19);
  const std::string d = parseJson(j);
  TEST_ASSERT_EQUAL_STRING("broken", strAt(d, "name").c_str());
  TEST_ASSERT_FALSE(boolAt(d, "valid"));
  TEST_ASSERT_TRUE(hasKey(d, "error"));
  TEST_ASSERT_TRUE(strAt(d, "error").size() > 0);
  TEST_ASSERT_EQUAL_UINT32(0u, uintAt(d, "notes"));
}

static void test_entry_json_escapes_the_content() {
  const std::string j = sounds::entryJson("odd", "a\"b\\c\nd", 7);
  const std::string d = parseJson(j);
  TEST_ASSERT_EQUAL_STRING("a\"b\\c\nd", strAt(d, "rtttl").c_str());
  TEST_ASSERT_FALSE(boolAt(d, "valid"));
}

static void test_prepare_write_accepts_a_two_part_string() {
  sounds::PutResult r = sounds::prepareWrite("doorbell", "{\"rtttl\":\"d=4,o=5,b=100:e,c\"}");
  TEST_ASSERT_TRUE_MESSAGE(r.ok, r.message.c_str());
  TEST_ASSERT_EQUAL_STRING("doorbell:d=4,o=5,b=100:e,c", r.content.c_str());
}

static void test_prepare_write_retitles_a_three_part_string() {
  sounds::PutResult r =
      sounds::prepareWrite("doorbell", "{\"rtttl\":\"something-else:d=4,o=5,b=100:e,c\"}");
  TEST_ASSERT_TRUE_MESSAGE(r.ok, r.message.c_str());
  TEST_ASSERT_EQUAL_STRING("doorbell:d=4,o=5,b=100:e,c", r.content.c_str());
}

static void test_prepare_write_rejects_a_bad_name() {
  sounds::PutResult r = sounds::prepareWrite("has space", "{\"rtttl\":\"d=4,o=5,b=100:e,c\"}");
  TEST_ASSERT_FALSE(r.ok);
  TEST_ASSERT_EQUAL_INT(422, r.status);
  TEST_ASSERT_EQUAL_STRING("name", r.field.c_str());
}

static void test_prepare_write_rejects_traversal_in_the_name() {
  TEST_ASSERT_FALSE(sounds::prepareWrite("../evil", "{\"rtttl\":\"d=4:c\"}").ok);
  TEST_ASSERT_FALSE(sounds::prepareWrite("a/b", "{\"rtttl\":\"d=4:c\"}").ok);
}

static void test_prepare_write_rejects_unparseable_rtttl() {
  sounds::PutResult r = sounds::prepareWrite("x", "{\"rtttl\":\"d=4,o=5,b=120:c,e,h\"}");
  TEST_ASSERT_FALSE(r.ok);
  TEST_ASSERT_EQUAL_INT(422, r.status);
  TEST_ASSERT_EQUAL_STRING("rtttl", r.field.c_str());
  TEST_ASSERT_EQUAL_STRING("validationFailed", r.code.c_str());
  TEST_ASSERT_TRUE(r.message.find("not a note") != std::string::npos);
  TEST_ASSERT_TRUE(r.message.find("offset") != std::string::npos);
}

static void test_prepare_write_rejects_a_missing_or_wrong_typed_key() {
  TEST_ASSERT_FALSE(sounds::prepareWrite("x", "{}").ok);
  TEST_ASSERT_FALSE(sounds::prepareWrite("x", "{\"rtttl\":42}").ok);
  TEST_ASSERT_FALSE(sounds::prepareWrite("x", "{\"rtttl\":\"\"}").ok);
}

static void test_prepare_write_rejects_a_broken_body() {
  sounds::PutResult r = sounds::prepareWrite("x", "{not json");
  TEST_ASSERT_FALSE(r.ok);
  TEST_ASSERT_EQUAL_INT(400, r.status);
  TEST_ASSERT_EQUAL_STRING("invalidJson", r.code.c_str());
}

static void test_prepare_write_rejects_an_overlong_melody() {
  std::string notes = "c";
  while (notes.size() < 600) notes += ",c";
  sounds::PutResult r = sounds::prepareWrite("x", "{\"rtttl\":\"d=4,o=5,b=120:" + notes + "\"}");
  TEST_ASSERT_FALSE(r.ok);
  TEST_ASSERT_EQUAL_INT(422, r.status);
}

static void test_prepare_write_round_trips_through_entry_json() {
  sounds::PutResult r = sounds::prepareWrite("bell", "{\"rtttl\":\"d=8,o=5,b=120:16c,16e\"}");
  TEST_ASSERT_TRUE(r.ok);
  const std::string d =
      parseJson(sounds::entryJson("bell", r.content, (uint32_t)r.content.size()));
  TEST_ASSERT_TRUE(boolAt(d, "valid"));
  TEST_ASSERT_EQUAL_UINT32(2u, uintAt(d, "notes"));
}

int main(int, char**) {
  UNITY_BEGIN();
  RUN_TEST(test_name_from_file);
  RUN_TEST(test_name_from_file_rejects_the_rest);
  RUN_TEST(test_path_for);
  RUN_TEST(test_entry_json_carries_the_derived_facts);
  RUN_TEST(test_entry_json_reports_a_broken_file_instead_of_hiding_it);
  RUN_TEST(test_entry_json_escapes_the_content);
  RUN_TEST(test_prepare_write_accepts_a_two_part_string);
  RUN_TEST(test_prepare_write_retitles_a_three_part_string);
  RUN_TEST(test_prepare_write_rejects_a_bad_name);
  RUN_TEST(test_prepare_write_rejects_traversal_in_the_name);
  RUN_TEST(test_prepare_write_rejects_unparseable_rtttl);
  RUN_TEST(test_prepare_write_rejects_a_missing_or_wrong_typed_key);
  RUN_TEST(test_prepare_write_rejects_a_broken_body);
  RUN_TEST(test_prepare_write_rejects_an_overlong_melody);
  RUN_TEST(test_prepare_write_round_trips_through_entry_json);
  return UNITY_END();
}
