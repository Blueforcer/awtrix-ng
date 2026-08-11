
#include <unity.h>

#include "core/api/StateJson.h"
#include "core/script/ScriptHeapTesting.h"
#include "core/script/SharedState.h"

using namespace awtrix::script;

void setUp() {}
void tearDown() { heap::testing::resetGrowthBudget(); }

static SharedState::Status set(SharedState& s, const char* owner, const char* key,
                               SharedState::Value v, int64_t nowMs = 0) {
  return s.set(owner, key, std::move(v), nowMs);
}

static void test_int_round_trip() {
  SharedState s;
  TEST_ASSERT_TRUE(set(s, "weather", "temp", SharedState::Value::ofInt(12)) ==
                   SharedState::Status::Ok);
  const SharedState::Value* v = s.find("weather", "temp");
  TEST_ASSERT_NOT_NULL(v);
  TEST_ASSERT_TRUE(v->type == SharedState::Type::Int);
  TEST_ASSERT_EQUAL_INT64(12, v->i);
}

static void test_types_stay_distinct() {
  SharedState s;
  set(s, "a", "i", SharedState::Value::ofInt(1));
  set(s, "a", "r", SharedState::Value::ofReal(1.5));
  set(s, "a", "b", SharedState::Value::ofBool(true));
  set(s, "a", "s", SharedState::Value::ofStr("hi"));

  TEST_ASSERT_TRUE(s.find("a", "i")->type == SharedState::Type::Int);
  TEST_ASSERT_TRUE(s.find("a", "r")->type == SharedState::Type::Real);
  TEST_ASSERT_TRUE(s.find("a", "b")->type == SharedState::Type::Bool);
  TEST_ASSERT_TRUE(s.find("a", "s")->type == SharedState::Type::Str);
  TEST_ASSERT_EQUAL_FLOAT(1.5f, static_cast<float>(s.find("a", "r")->r));
  TEST_ASSERT_EQUAL_INT64(1, s.find("a", "b")->i);
  TEST_ASSERT_EQUAL_STRING("hi", s.find("a", "s")->s.c_str());
}

static void test_missing_key_and_missing_owner_are_null() {
  SharedState s;
  set(s, "a", "k", SharedState::Value::ofInt(1));
  TEST_ASSERT_NULL(s.find("a", "nope"));
  TEST_ASSERT_NULL(s.find("nope", "k"));
}

static void test_overwrite_replaces_value_and_timestamp() {
  SharedState s;
  set(s, "a", "k", SharedState::Value::ofInt(1), 1000);
  set(s, "a", "k", SharedState::Value::ofStr("later"), 4000);
  const SharedState::Value* v = s.find("a", "k");
  TEST_ASSERT_TRUE(v->type == SharedState::Type::Str);
  TEST_ASSERT_EQUAL_STRING("later", v->s.c_str());
  TEST_ASSERT_EQUAL_INT32(4000, v->writtenMs);
  TEST_ASSERT_EQUAL_UINT32(1, s.entries());
}

static void test_owners_do_not_collide() {
  SharedState s;
  set(s, "clock", "mode", SharedState::Value::ofStr("24h"));
  set(s, "weather", "mode", SharedState::Value::ofStr("metric"));
  TEST_ASSERT_EQUAL_STRING("24h", s.find("clock", "mode")->s.c_str());
  TEST_ASSERT_EQUAL_STRING("metric", s.find("weather", "mode")->s.c_str());
  TEST_ASSERT_EQUAL_UINT32(2, s.entries());
}

static void test_purge_drops_only_that_owner() {
  SharedState s;
  set(s, "clock", "mode", SharedState::Value::ofInt(1));
  set(s, "weather", "temp", SharedState::Value::ofInt(2));
  s.purge("clock");
  TEST_ASSERT_NULL(s.find("clock", "mode"));
  TEST_ASSERT_NOT_NULL(s.find("weather", "temp"));
  TEST_ASSERT_EQUAL_UINT32(1, s.entries());
}

static void test_purge_of_unknown_owner_is_harmless() {
  SharedState s;
  set(s, "a", "k", SharedState::Value::ofInt(1));
  s.purge("ghost");
  TEST_ASSERT_EQUAL_UINT32(1, s.entries());
}

static void test_erase_removes_the_key() {
  SharedState s;
  set(s, "a", "k", SharedState::Value::ofInt(1));
  s.erase("a", "k");
  TEST_ASSERT_NULL(s.find("a", "k"));
  TEST_ASSERT_EQUAL_UINT32(0, s.entries());
}

static void test_erasing_the_last_key_drops_the_namespace() {
  SharedState s;
  set(s, "a", "k", SharedState::Value::ofInt(1));
  s.erase("a", "k");
  TEST_ASSERT_TRUE(s.keys().empty());
}

static void test_key_must_not_be_empty() {
  SharedState s;
  TEST_ASSERT_TRUE(set(s, "a", "", SharedState::Value::ofInt(1)) ==
                   SharedState::Status::InvalidKey);
}

static void test_key_must_not_contain_a_dot() {
  SharedState s;
  TEST_ASSERT_TRUE(set(s, "a", "a.b", SharedState::Value::ofInt(1)) ==
                   SharedState::Status::InvalidKey);
}

static void test_key_rejects_spaces_and_control_characters() {
  SharedState s;
  TEST_ASSERT_TRUE(set(s, "a", "two words", SharedState::Value::ofInt(1)) ==
                   SharedState::Status::InvalidKey);
  TEST_ASSERT_TRUE(set(s, "a", "line\nbreak", SharedState::Value::ofInt(1)) ==
                   SharedState::Status::InvalidKey);
}

static void test_key_accepts_alnum_underscore_and_dash() {
  SharedState s;
  TEST_ASSERT_TRUE(set(s, "a", "temp_out-2", SharedState::Value::ofInt(1)) ==
                   SharedState::Status::Ok);
}

static void test_key_length_is_capped() {
  SharedState s;
  const std::string ok(kMaxSharedKeyChars, 'k');
  const std::string tooLong(kMaxSharedKeyChars + 1, 'k');
  TEST_ASSERT_TRUE(set(s, "a", ok.c_str(), SharedState::Value::ofInt(1)) ==
                   SharedState::Status::Ok);
  TEST_ASSERT_TRUE(set(s, "a", tooLong.c_str(), SharedState::Value::ofInt(1)) ==
                   SharedState::Status::InvalidKey);
}

static void test_owner_must_not_be_empty() {
  SharedState s;
  TEST_ASSERT_TRUE(set(s, "", "k", SharedState::Value::ofInt(1)) ==
                   SharedState::Status::InvalidKey);
}

static void test_byte_budget_counts_keys_and_strings() {
  SharedState s;
  set(s, "a", "k", SharedState::Value::ofStr("1234"));
  TEST_ASSERT_EQUAL_UINT32(5, s.bytes("a"));
  set(s, "a", "n", SharedState::Value::ofInt(999999));
  TEST_ASSERT_EQUAL_UINT32(6, s.bytes("a"));
}

static void test_many_shared_keys_are_accepted() {
  SharedState s;
  for (int i = 0; i < 64; ++i) {
    const std::string k = "k" + std::to_string(i);
    TEST_ASSERT_EQUAL(static_cast<int>(SharedState::Status::Ok),
                      static_cast<int>(s.set("app", k, SharedState::Value::ofInt(i), 0)));
  }
  TEST_ASSERT_EQUAL_size_t(64u, s.keys("app").size());
}

// Published values are copied into a map the script heap never reclaims, so a write that would
// not fit is refused instead of taken.
static void test_a_write_that_does_not_fit_is_refused() {
  SharedState s;
  heap::testing::setGrowthBudget(100);
  const std::string big(200, 'x');
  TEST_ASSERT_EQUAL(static_cast<int>(SharedState::Status::NoRoom),
                    static_cast<int>(s.set("a", "k", SharedState::Value::ofStr(big), 0)));
  TEST_ASSERT_NULL(s.find("a", "k"));
  TEST_ASSERT_EQUAL_UINT32(0u, static_cast<uint32_t>(s.bytes("a")));
}

// The refusal weighs what the app already published too: three writes that each fit on their own
// still have to fit together, which is the case a script builds up one string at a time.
static void test_writes_are_weighed_against_what_the_app_already_holds() {
  SharedState s;
  heap::testing::setGrowthBudget(100);
  const std::string chunk(40, 'x');
  TEST_ASSERT_EQUAL(static_cast<int>(SharedState::Status::Ok),
                    static_cast<int>(s.set("a", "a1", SharedState::Value::ofStr(chunk), 0)));
  TEST_ASSERT_EQUAL(static_cast<int>(SharedState::Status::Ok),
                    static_cast<int>(s.set("a", "a2", SharedState::Value::ofStr(chunk), 0)));
  TEST_ASSERT_EQUAL(static_cast<int>(SharedState::Status::NoRoom),
                    static_cast<int>(s.set("a", "a3", SharedState::Value::ofStr(chunk), 0)));
  TEST_ASSERT_EQUAL_UINT32(2u, static_cast<uint32_t>(s.entries()));
}

// Replacing a value hands the old one back, so a script that refreshes the same key every minute
// keeps working at the edge instead of being refused for bytes it is about to release.
static void test_replacing_a_value_is_not_charged_twice() {
  SharedState s;
  heap::testing::setGrowthBudget(100);
  const std::string big(90, 'x');
  TEST_ASSERT_EQUAL(static_cast<int>(SharedState::Status::Ok),
                    static_cast<int>(s.set("a", "k", SharedState::Value::ofStr(big), 0)));
  for (int i = 0; i < 5; ++i)
    TEST_ASSERT_EQUAL(static_cast<int>(SharedState::Status::Ok),
                      static_cast<int>(s.set("a", "k", SharedState::Value::ofStr(big), 0)));
  TEST_ASSERT_EQUAL_UINT32(91u, static_cast<uint32_t>(s.bytes("a")));
}

// Each app is weighed on its own: one that has published a lot does not lock out the others.
static void test_one_app_filling_up_does_not_refuse_another() {
  SharedState s;
  heap::testing::setGrowthBudget(100);
  TEST_ASSERT_EQUAL(static_cast<int>(SharedState::Status::Ok),
                    static_cast<int>(s.set("a", "k", SharedState::Value::ofStr(std::string(90, 'x')),
                                           0)));
  TEST_ASSERT_EQUAL(static_cast<int>(SharedState::Status::Ok),
                    static_cast<int>(s.set("b", "k", SharedState::Value::ofStr(std::string(90, 'y')),
                                           0)));
}

static void test_keys_are_qualified_and_sorted() {
  SharedState s;
  set(s, "weather", "temp", SharedState::Value::ofInt(1));
  set(s, "clock", "mode", SharedState::Value::ofInt(1));
  set(s, "clock", "alarm", SharedState::Value::ofInt(1));
  const std::vector<std::string> k = s.keys();
  TEST_ASSERT_EQUAL_UINT32(3, k.size());
  TEST_ASSERT_EQUAL_STRING("clock.alarm", k[0].c_str());
  TEST_ASSERT_EQUAL_STRING("clock.mode", k[1].c_str());
  TEST_ASSERT_EQUAL_STRING("weather.temp", k[2].c_str());
}

static void test_keys_can_be_filtered_by_owner() {
  SharedState s;
  set(s, "weather", "temp", SharedState::Value::ofInt(1));
  set(s, "clock", "mode", SharedState::Value::ofInt(1));
  const std::vector<std::string> k = s.keys("clock");
  TEST_ASSERT_EQUAL_UINT32(1, k.size());
  TEST_ASSERT_EQUAL_STRING("clock.mode", k[0].c_str());
}

static void test_keys_of_an_unknown_owner_is_empty() {
  SharedState s;
  set(s, "a", "k", SharedState::Value::ofInt(1));
  TEST_ASSERT_TRUE(s.keys("ghost").empty());
}

static void test_a_bare_name_resolves_to_the_calling_app() {
  std::string owner, key;
  SharedState::splitQualified("temp", "weather", owner, key);
  TEST_ASSERT_EQUAL_STRING("weather", owner.c_str());
  TEST_ASSERT_EQUAL_STRING("temp", key.c_str());
}

static void test_a_dotted_name_resolves_to_the_named_app() {
  std::string owner, key;
  SharedState::splitQualified("weather.temp", "clock", owner, key);
  TEST_ASSERT_EQUAL_STRING("weather", owner.c_str());
  TEST_ASSERT_EQUAL_STRING("temp", key.c_str());
}

static void test_a_dotted_app_name_still_resolves() {
  std::string owner, key;
  SharedState::splitQualified("my.weather.temp", "clock", owner, key);
  TEST_ASSERT_EQUAL_STRING("my.weather", owner.c_str());
  TEST_ASSERT_EQUAL_STRING("temp", key.c_str());
}

static void test_a_trailing_dot_yields_an_empty_key() {
  std::string owner, key;
  SharedState::splitQualified("weather.", "clock", owner, key);
  TEST_ASSERT_EQUAL_STRING("weather", owner.c_str());
  TEST_ASSERT_EQUAL_STRING("", key.c_str());
}

static void test_clear_empties_everything() {
  SharedState s;
  set(s, "a", "k", SharedState::Value::ofInt(1));
  set(s, "b", "k", SharedState::Value::ofInt(1));
  s.clear();
  TEST_ASSERT_EQUAL_UINT32(0, s.entries());
  TEST_ASSERT_TRUE(s.keys().empty());
}

static void test_snapshot_resolves_ages() {
  SharedState s;
  set(s, "weather", "temp", SharedState::Value::ofInt(12), 1000);
  set(s, "clock", "mode", SharedState::Value::ofStr("24h"), 3000);

  const std::vector<SharedEntry> e = snapshot(s, 5000);
  TEST_ASSERT_EQUAL_UINT32(2, e.size());
  TEST_ASSERT_EQUAL_STRING("clock", e[0].owner.c_str());
  TEST_ASSERT_EQUAL_STRING("mode", e[0].key.c_str());
  TEST_ASSERT_EQUAL_INT32(2000, e[0].ageMs);
  TEST_ASSERT_EQUAL_STRING("weather", e[1].owner.c_str());
  TEST_ASSERT_EQUAL_INT32(4000, e[1].ageMs);
}

static void test_snapshot_of_an_empty_space_is_empty() {
  SharedState s;
  TEST_ASSERT_TRUE(snapshot(s, 0).empty());
}

static void test_shared_json_carries_owner_key_type_value_and_age() {
  SharedState s;
  set(s, "weather", "temp", SharedState::Value::ofInt(12), 1000);
  const std::string json = awtrix::buildSharedStateJson(snapshot(s, 4200));
  TEST_ASSERT_EQUAL_STRING(
      "[{\"owner\":\"weather\",\"key\":\"temp\",\"type\":\"int\",\"value\":12,\"ageMs\":3200}]",
      json.c_str());
}

static void test_shared_json_types_are_native() {
  SharedState s;
  set(s, "a", "b", SharedState::Value::ofBool(true), 0);
  set(s, "a", "s", SharedState::Value::ofStr("hi"), 0);
  const std::string json = awtrix::buildSharedStateJson(snapshot(s, 0));
  TEST_ASSERT_EQUAL_STRING(
      "[{\"owner\":\"a\",\"key\":\"b\",\"type\":\"bool\",\"value\":true,\"ageMs\":0},"
      "{\"owner\":\"a\",\"key\":\"s\",\"type\":\"string\",\"value\":\"hi\",\"ageMs\":0}]",
      json.c_str());
}

static void test_empty_shared_json_is_an_empty_array() {
  SharedState s;
  TEST_ASSERT_EQUAL_STRING("[]", awtrix::buildSharedStateJson(snapshot(s, 0)).c_str());
}

int main(int, char**) {
  UNITY_BEGIN();
  RUN_TEST(test_int_round_trip);
  RUN_TEST(test_types_stay_distinct);
  RUN_TEST(test_missing_key_and_missing_owner_are_null);
  RUN_TEST(test_overwrite_replaces_value_and_timestamp);
  RUN_TEST(test_owners_do_not_collide);
  RUN_TEST(test_purge_drops_only_that_owner);
  RUN_TEST(test_purge_of_unknown_owner_is_harmless);
  RUN_TEST(test_erase_removes_the_key);
  RUN_TEST(test_erasing_the_last_key_drops_the_namespace);
  RUN_TEST(test_key_must_not_be_empty);
  RUN_TEST(test_key_must_not_contain_a_dot);
  RUN_TEST(test_key_rejects_spaces_and_control_characters);
  RUN_TEST(test_key_accepts_alnum_underscore_and_dash);
  RUN_TEST(test_key_length_is_capped);
  RUN_TEST(test_owner_must_not_be_empty);
  RUN_TEST(test_byte_budget_counts_keys_and_strings);
  RUN_TEST(test_many_shared_keys_are_accepted);
  RUN_TEST(test_a_write_that_does_not_fit_is_refused);
  RUN_TEST(test_writes_are_weighed_against_what_the_app_already_holds);
  RUN_TEST(test_replacing_a_value_is_not_charged_twice);
  RUN_TEST(test_one_app_filling_up_does_not_refuse_another);
  RUN_TEST(test_keys_are_qualified_and_sorted);
  RUN_TEST(test_keys_can_be_filtered_by_owner);
  RUN_TEST(test_keys_of_an_unknown_owner_is_empty);
  RUN_TEST(test_a_bare_name_resolves_to_the_calling_app);
  RUN_TEST(test_a_dotted_name_resolves_to_the_named_app);
  RUN_TEST(test_a_dotted_app_name_still_resolves);
  RUN_TEST(test_a_trailing_dot_yields_an_empty_key);
  RUN_TEST(test_clear_empties_everything);
  RUN_TEST(test_snapshot_resolves_ages);
  RUN_TEST(test_snapshot_of_an_empty_space_is_empty);
  RUN_TEST(test_shared_json_carries_owner_key_type_value_and_age);
  RUN_TEST(test_shared_json_types_are_native);
  RUN_TEST(test_empty_shared_json_is_an_empty_array);
  return UNITY_END();
}
