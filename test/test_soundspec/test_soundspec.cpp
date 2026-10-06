#include <unity.h>

#include <string>

#include "core/sound/SoundSpec.h"

using namespace awtrix;
using namespace awtrix::sound;

void setUp() {}
void tearDown() {}

namespace {

Choices ok(const char* json, Origin origin = Origin::Play) {
  Choices c;
  DispatchDetail d;
  TEST_ASSERT_TRUE_MESSAGE(parse(json, origin, c, d), (json + std::string(" -> ") + d.message).c_str());
  return c;
}

DispatchDetail bad(const char* json, Origin origin = Origin::Play) {
  Choices c;
  DispatchDetail d;
  TEST_ASSERT_FALSE_MESSAGE(parse(json, origin, c, d), json);
  return d;
}

}

// A plain string is the file shorthand.
static void test_a_string_is_a_file() {
  const Choices c = ok("\"ding\"");
  TEST_ASSERT_EQUAL_UINT8(1, c.count);
  TEST_ASSERT_EQUAL_INT((int)Kind::File, (int)c.items[0].kind);
  TEST_ASSERT_EQUAL_STRING("ding", c.items[0].text.c_str());
  TEST_ASSERT_FALSE(c.items[0].loop);
}

static void test_each_source_key_reads_its_value() {
  TEST_ASSERT_EQUAL_STRING("Racer/boost", ok("{\"file\":\"Racer/boost\"}").items[0].text.c_str());
  TEST_ASSERT_EQUAL_STRING("https://x.de/a.mp3",
                           ok("{\"file\":\"https://x.de/a.mp3\"}").items[0].text.c_str());
  TEST_ASSERT_EQUAL_INT((int)Kind::Rtttl, (int)ok("{\"rtttl\":\"a:d=4,o=5,b=120:c\"}").items[0].kind);
  TEST_ASSERT_EQUAL_INT((int)Kind::Song, (int)ok("{\"song\":\"bpm 120\"}").items[0].kind);
  TEST_ASSERT_EQUAL_STRING("Hello", ok("{\"speech\":\"Hello\"}").items[0].text.c_str());
  TEST_ASSERT_EQUAL_INT(7, ok("{\"track\":7}").items[0].number);
  TEST_ASSERT_EQUAL_STRING("SWR3", ok("{\"station\":\"SWR3\"}").items[0].text.c_str());
  const Choices byIndex = ok("{\"station\":2}");
  TEST_ASSERT_EQUAL_INT(2, byIndex.items[0].number);
  TEST_ASSERT_TRUE(byIndex.isStation());
}

static void test_loop_is_read() {
  TEST_ASSERT_TRUE(ok("{\"file\":\"siren\",\"loop\":true}").items[0].loop);
  TEST_ASSERT_FALSE(ok("{\"file\":\"siren\",\"loop\":false}").items[0].loop);
}

// A list plays its first playable entry; the parser keeps them in order.
static void test_a_list_keeps_up_to_four_entries_in_order() {
  const Choices c = ok("[{\"speech\":\"Door\"},\"ding\",{\"rtttl\":\"a:d=4,o=5,b=120:c\"}]");
  TEST_ASSERT_EQUAL_UINT8(3, c.count);
  TEST_ASSERT_EQUAL_INT((int)Kind::Speech, (int)c.items[0].kind);
  TEST_ASSERT_EQUAL_INT((int)Kind::File, (int)c.items[1].kind);
  TEST_ASSERT_EQUAL_INT((int)Kind::Rtttl, (int)c.items[2].kind);
}

static void test_mistakes_name_their_field() {
  DispatchDetail d = bad("{\"file\":\"a\",\"rtttl\":\"x:d=4:c\"}");
  TEST_ASSERT_EQUAL_STRING("file", d.field.c_str());
  TEST_ASSERT_EQUAL_STRING("one sound key only", d.message.c_str());

  d = bad("{\"loop\":true}");
  TEST_ASSERT_EQUAL_STRING("", d.field.c_str());
  TEST_ASSERT_EQUAL_STRING("needs a sound key", d.message.c_str());

  d = bad("{\"mp3\":\"ding\"}");
  TEST_ASSERT_EQUAL_STRING("mp3", d.field.c_str());
  TEST_ASSERT_EQUAL_STRING("unknown field", d.message.c_str());

  d = bad("{\"file\":\"my song\"}");
  TEST_ASSERT_EQUAL_STRING("file", d.field.c_str());
  TEST_ASSERT_EQUAL_STRING("invalid name", d.message.c_str());

  d = bad("{\"rtttl\":\"d=4,o=5,b=120:c\"}");
  TEST_ASSERT_EQUAL_STRING("rtttl", d.field.c_str());

  d = bad("{\"track\":3000}");
  TEST_ASSERT_EQUAL_STRING("track", d.field.c_str());
  TEST_ASSERT_EQUAL_STRING("must be 1..2999", d.message.c_str());

  d = bad("{\"file\":\"a\",\"loop\":1}");
  TEST_ASSERT_EQUAL_STRING("loop", d.field.c_str());
  TEST_ASSERT_EQUAL_STRING("must be true or false", d.message.c_str());

  d = bad("5");
  TEST_ASSERT_EQUAL_STRING("must be a string, object or list", d.message.c_str());

  d = bad("[]");
  TEST_ASSERT_EQUAL_STRING("must have 1 to 4 entries", d.message.c_str());
  d = bad("[\"a\",\"b\",\"c\",\"d\",\"e\"]");
  TEST_ASSERT_EQUAL_STRING("must have 1 to 4 entries", d.message.c_str());
}

static void test_fields_inside_a_list_carry_the_index() {
  DispatchDetail d = bad("[\"ding\",{\"rtttl\":\"broken\"}]");
  TEST_ASSERT_EQUAL_STRING("[1].rtttl", d.field.c_str());
  d = bad("[\"ding\",{\"rtttl\":\"broken\"}]", Origin::Notification);
  TEST_ASSERT_EQUAL_STRING("sound[1].rtttl", d.field.c_str());
  d = bad("{\"rtttl\":\"broken\"}", Origin::Notification);
  TEST_ASSERT_EQUAL_STRING("sound.rtttl", d.field.c_str());
}

static void test_station_only_alone_and_never_in_a_notification() {
  DispatchDetail d = bad("[{\"station\":\"SWR3\"},\"ding\"]");
  TEST_ASSERT_EQUAL_STRING("[0].station", d.field.c_str());
  TEST_ASSERT_EQUAL_STRING("not here", d.message.c_str());
  d = bad("{\"station\":\"SWR3\"}", Origin::Notification);
  TEST_ASSERT_EQUAL_STRING("sound.station", d.field.c_str());
  d = bad("{\"station\":\"SWR3\",\"loop\":true}");
  TEST_ASSERT_EQUAL_STRING("loop", d.field.c_str());
  ok("{\"station\":\"SWR3\"}", Origin::Script);
}

// nextBar exists for a script's looping song only.
static void test_next_bar_only_for_a_scripts_looping_song() {
  const Choices c = ok("{\"song\":\"bpm 90\",\"loop\":true,\"nextBar\":true}", Origin::Script);
  TEST_ASSERT_TRUE(c.items[0].nextBar);
  DispatchDetail d = bad("{\"song\":\"bpm 90\",\"loop\":true,\"nextBar\":true}");
  TEST_ASSERT_EQUAL_STRING("nextBar", d.field.c_str());
  TEST_ASSERT_EQUAL_STRING("only with a looping song", d.message.c_str());
  d = bad("{\"song\":\"bpm 90\",\"nextBar\":true}", Origin::Script);
  TEST_ASSERT_EQUAL_STRING("nextBar", d.field.c_str());
}

static void test_speech_length_is_bounded() {
  bad("{\"speech\":\"\"}");
  const std::string longText = "{\"speech\":\"" + std::string(513, 'a') + "\"}";
  DispatchDetail d = bad(longText.c_str());
  TEST_ASSERT_EQUAL_STRING("speech", d.field.c_str());
  TEST_ASSERT_EQUAL_STRING("must be 1..512 bytes", d.message.c_str());
}

static void test_display_names() {
  TEST_ASSERT_EQUAL_STRING("ding", displayName(ok("\"ding\"").items[0]).c_str());
  TEST_ASSERT_EQUAL_STRING("rtttl", displayName(ok("{\"rtttl\":\"a:d=4,o=5,b=120:c\"}").items[0]).c_str());
  TEST_ASSERT_EQUAL_STRING("speech", displayName(ok("{\"speech\":\"Hi\"}").items[0]).c_str());
  TEST_ASSERT_EQUAL_STRING("7", displayName(ok("{\"track\":7}").items[0]).c_str());
}

int main(int, char**) {
  UNITY_BEGIN();
  RUN_TEST(test_a_string_is_a_file);
  RUN_TEST(test_each_source_key_reads_its_value);
  RUN_TEST(test_loop_is_read);
  RUN_TEST(test_a_list_keeps_up_to_four_entries_in_order);
  RUN_TEST(test_mistakes_name_their_field);
  RUN_TEST(test_fields_inside_a_list_carry_the_index);
  RUN_TEST(test_station_only_alone_and_never_in_a_notification);
  RUN_TEST(test_next_bar_only_for_a_scripts_looping_song);
  RUN_TEST(test_speech_length_is_bounded);
  RUN_TEST(test_display_names);
  return UNITY_END();
}
