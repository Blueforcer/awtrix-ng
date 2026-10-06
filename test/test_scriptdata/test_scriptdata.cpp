#include <string>

#include <unity.h>

#include "core/script/ScriptData.h"
#include "platform/linux/host/HostScriptHeap.h"

using namespace awtrix;

void setUp() {}
void tearDown() {}

static const char* kGame =
    "# @name Game\n"
    "# @config speed number default=3\n"
    "class Game\n"
    "end\n";

static std::string dataOf(const std::string& store) {
  std::string out;
  TEST_ASSERT_TRUE(script::appendDataJson(out, script::parseConfig(kGame), store));
  return out;
}

static script::StorePatch patch(const std::string& store, const std::string& body) {
  return script::applyDataPatch(script::parseConfig(kGame), store, body);
}

static void test_data_leaves_out_settings_and_cleared_keys() {
  TEST_ASSERT_EQUAL_STRING("{\"best\":[3,1],\"name\":\"Ada\"}",
                           dataOf("{\"speed\":5,\"best\":[3,1],\"cup\":null,\"name\":\"Ada\"}").c_str());
}

static void test_a_damaged_or_missing_store_reads_as_empty() {
  TEST_ASSERT_EQUAL_STRING("{}", dataOf("{\"best\":[3,1").c_str());
  TEST_ASSERT_EQUAL_STRING("{}", dataOf("[1,2]").c_str());
  TEST_ASSERT_EQUAL_STRING("{}", dataOf("").c_str());
}

static void test_an_escaped_key_is_judged_by_what_it_spells() {
  TEST_ASSERT_EQUAL_STRING("{}", dataOf("{\"sp\\u0065ed\":5}").c_str());
  TEST_ASSERT_EQUAL_STRING("{\"a\\\"b\":1}", dataOf("{\"a\\\"b\":1}").c_str());
}

static void test_data_stops_when_the_heap_is_tight() {
  script::heap::testing::setGrowthBudget(8);
  std::string out;
  const bool complete =
      script::appendDataJson(out, script::parseConfig(kGame), "{\"best\":[1,2,3,4,5,6]}");
  script::heap::testing::resetGrowthBudget();
  TEST_ASSERT_FALSE(complete);
}

static void test_patch_sets_removes_and_keeps_the_rest() {
  const script::StorePatch r = patch("{\"speed\":5,\"best\":[3,1],\"unl\":2,\"cont\":1}",
                                     "{\"unl\":9,\"best\":null,\"new\":{\"a\":1}}");
  TEST_ASSERT_TRUE(r.ok);
  TEST_ASSERT_EQUAL_STRING("{\"speed\":5,\"unl\":9,\"cont\":1,\"new\":{\"a\":1}}",
                           r.storeJson.c_str());
}

static void test_removing_a_key_that_is_not_there_changes_nothing() {
  const script::StorePatch r = patch("{\"unl\":2}", "{\"gone\":null}");
  TEST_ASSERT_TRUE(r.ok);
  TEST_ASSERT_EQUAL_STRING("{\"unl\":2}", r.storeJson.c_str());
}

static void test_the_last_mention_of_a_key_wins() {
  const script::StorePatch r = patch("{\"unl\":1,\"unl\":2}", "{\"unl\":3,\"unl\":4}");
  TEST_ASSERT_TRUE(r.ok);
  TEST_ASSERT_EQUAL_STRING("{\"unl\":4}", r.storeJson.c_str());
}

static void test_patch_refuses_a_setting_however_it_is_spelled() {
  const script::StorePatch plain = patch("{\"speed\":5}", "{\"unl\":1,\"speed\":9}");
  TEST_ASSERT_FALSE(plain.ok);
  TEST_ASSERT_EQUAL_STRING("speed", plain.field.c_str());
  TEST_ASSERT_TRUE(plain.message.find("/config") != std::string::npos);
  TEST_ASSERT_FALSE(patch("{}", "{\"sp\\u0065ed\":9}").ok);
}

static void test_patch_refuses_what_is_not_an_object() {
  TEST_ASSERT_FALSE(patch("{}", "[1,2]").ok);
  TEST_ASSERT_FALSE(patch("{}", "[1,2]").malformed);
  TEST_ASSERT_FALSE(patch("{}", "{oops}").ok);
  TEST_ASSERT_TRUE(patch("{}", "{oops}").malformed);
  TEST_ASSERT_FALSE(patch("{}", "7").ok);
}

static void test_patch_onto_a_damaged_store_starts_from_empty() {
  const script::StorePatch r = patch("{\"best\":[3", "{\"unl\":1}");
  TEST_ASSERT_TRUE(r.ok);
  TEST_ASSERT_EQUAL_STRING("{\"unl\":1}", r.storeJson.c_str());
}

static void test_the_data_route_answers_and_names_its_failures() {
  const script::ConfigTextFn source = [](const std::string& n, std::string& out) {
    if (n != "Game") return false;
    out = kGame;
    return true;
  };
  const script::ConfigTextFn store = [](const std::string&, std::string& out) {
    out = "{\"speed\":5,\"unl\":2}";
    return true;
  };
  std::string body;
  TEST_ASSERT_EQUAL_INT(200, script::dataResponse("Game", source, store, body));
  TEST_ASSERT_EQUAL_STRING("{\"unl\":2}", body.c_str());
  TEST_ASSERT_EQUAL_INT(404, script::dataResponse("Other", source, store, body));
  TEST_ASSERT_EQUAL_INT(400, script::dataResponse("../x", source, store, body));
  TEST_ASSERT_EQUAL_INT(503, script::dataResponse("Game", nullptr, store, body));

  script::heap::testing::setGrowthBudget(4);
  const int tight = script::dataResponse("Game", source, store, body);
  script::heap::testing::resetGrowthBudget();
  TEST_ASSERT_EQUAL_INT(507, tight);
  TEST_ASSERT_TRUE(body.find("\"code\":\"insufficientStorage\"") != std::string::npos);
}

int main() {
  UNITY_BEGIN();
  RUN_TEST(test_data_leaves_out_settings_and_cleared_keys);
  RUN_TEST(test_a_damaged_or_missing_store_reads_as_empty);
  RUN_TEST(test_an_escaped_key_is_judged_by_what_it_spells);
  RUN_TEST(test_data_stops_when_the_heap_is_tight);
  RUN_TEST(test_patch_sets_removes_and_keeps_the_rest);
  RUN_TEST(test_removing_a_key_that_is_not_there_changes_nothing);
  RUN_TEST(test_the_last_mention_of_a_key_wins);
  RUN_TEST(test_patch_refuses_a_setting_however_it_is_spelled);
  RUN_TEST(test_patch_refuses_what_is_not_an_object);
  RUN_TEST(test_patch_onto_a_damaged_store_starts_from_empty);
  RUN_TEST(test_the_data_route_answers_and_names_its_failures);
  return UNITY_END();
}
