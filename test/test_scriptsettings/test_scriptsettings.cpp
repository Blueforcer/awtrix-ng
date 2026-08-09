#include <unity.h>

#include <string>
#include <vector>

#include "core/Settings.h"
#include "core/apps/IApp.h"
#include "core/script/BerryVM.h"
#include "core/script/ScriptBindings.h"
#include "core/script/ScriptServices.h"

using namespace awtrix;

static script::ScriptServices g_svc;
static Settings g_set;
static std::string g_log;
static std::vector<std::string> g_writes;
static bool g_writeAccepts = true;

struct SoundCall {
  script::SoundAction action;
  std::string payload;
};
static std::vector<SoundCall> g_sounds;
static bool g_soundAccepts = true;

void setUp() {
  g_set = Settings{};
  g_log.clear();
  g_writes.clear();
  g_writeAccepts = true;
  g_sounds.clear();
  g_soundAccepts = true;

  g_svc = script::ScriptServices{};
  g_svc.settings = [] { return &g_set; };
  g_svc.setSettings = [](const std::string& json) {
    g_writes.push_back(json);
    return g_writeAccepts;
  };
  g_svc.sound = [](script::SoundAction a, const std::string& payload) {
    g_sounds.push_back({a, payload});
    return g_soundAccepts;
  };
  g_svc.log = [](const std::string& s) { g_log += s; };
  script::setServices(&g_svc);
}
void tearDown() { script::setServices(nullptr); }

static std::string run(const char* body) {
  script::BerryVM vm;
  std::string err;
  TEST_ASSERT_TRUE_MESSAGE(script::installBindings(vm, err), err.c_str());
  const std::string src = std::string("def draw() ") + body + " end";
  TEST_ASSERT_TRUE_MESSAGE(vm.load(src.c_str()), vm.lastError().c_str());
  script::BindingScope s(nullptr, nullptr, "T");
  TEST_ASSERT_TRUE_MESSAGE(vm.call("draw"), vm.lastError().c_str());
  return g_log;
}

static bool logged(const char* needle) { return g_log.find(needle) != std::string::npos; }

static void test_get_answers_the_api_keys() {
  g_set.textColor = 0x00FF80u;
  g_set.appDurationMs = 4500;
  g_set.useCelsius = false;
  run("log(str(settings.get('textColor'))) log('/') "
      "log(str(settings.get('appDurationMs'))) log('/') "
      "log(str(settings.get('useCelsius')))");
  TEST_ASSERT_TRUE(logged("65408"));
  TEST_ASSERT_TRUE(logged("4500"));
  TEST_ASSERT_TRUE(logged("false"));
}

static void test_get_covers_every_field_kind() {
  g_set.brightness = 200;
  g_set.gamma = 2.5f;
  run("log(str(settings.get('brightness'))) log('/') "
      "log(str(settings.get('autoBrightness'))) log('/') "
      "log(str(settings.get('gamma'))) log('/') "
      "log(settings.get('timeSeparatorMode')) log('/') "
      "log(settings.get('transitionEffect'))");
  TEST_ASSERT_TRUE(logged("200"));
  TEST_ASSERT_TRUE(logged("false"));
  TEST_ASSERT_TRUE(logged("2.5"));
  TEST_ASSERT_TRUE(logged("pulse"));
  TEST_ASSERT_TRUE(logged("Rain"));
}

static void test_get_of_an_unknown_key_is_nil() {
  run("log(str(settings.get('nonesuch')) + '/' + str(settings.get('scroll')) + '/' + "
      "str(settings.get('weekdayBar')) + '/' + str(settings.get('')))");
  TEST_ASSERT_TRUE(logged("nil/nil/nil/nil"));
}

static void test_unset_accent_colour_reads_nil() {
  run("log(str(settings.get('timeColor')))");
  TEST_ASSERT_TRUE(logged("nil"));
  g_log.clear();
  g_set.timeColor.set = true;
  g_set.timeColor.rgb = 0xFF0000u;
  run("log(str(settings.get('timeColor')))");
  TEST_ASSERT_TRUE(logged("16711680"));
}

static void test_set_queues_the_canonical_patch() {
  run("log(str(settings.set('brightness', 40)))");
  TEST_ASSERT_TRUE(logged("true"));
  TEST_ASSERT_EQUAL_INT(1, static_cast<int>(g_writes.size()));
  TEST_ASSERT_EQUAL_STRING("{\"brightness\":40}", g_writes[0].c_str());
}

static void test_set_carries_every_value_shape() {
  run("settings.set('uppercase', false) settings.set('gamma', 2.5) "
      "settings.set('timeSeparatorMode', 'blink') settings.set('textColor', '#FF0000')");
  TEST_ASSERT_EQUAL_INT(4, static_cast<int>(g_writes.size()));
  TEST_ASSERT_EQUAL_STRING("{\"uppercase\":false}", g_writes[0].c_str());
  TEST_ASSERT_EQUAL_STRING("{\"gamma\":2.5}", g_writes[1].c_str());
  TEST_ASSERT_EQUAL_STRING("{\"timeSeparatorMode\":\"blink\"}", g_writes[2].c_str());
  TEST_ASSERT_EQUAL_STRING("{\"textColor\":\"#FF0000\"}", g_writes[3].c_str());
}

static void test_set_nil_clears_an_accent_colour() {
  g_set.timeColor.set = true;
  g_set.timeColor.rgb = 0xFF0000u;
  run("log(str(settings.set('timeColor', nil)))");
  TEST_ASSERT_TRUE(logged("true"));
  TEST_ASSERT_EQUAL_INT(1, static_cast<int>(g_writes.size()));
  TEST_ASSERT_EQUAL_STRING("{\"timeColor\":null}", g_writes[0].c_str());
}

static void test_set_rejects_what_the_rest_api_rejects() {
  run("log(str(settings.set('nonesuch', 1))) "
      "log(str(settings.set('brightness', 999))) "
      "log(str(settings.set('brightness', 'bright'))) "
      "log(str(settings.set('uppercase', 3))) "
      "log(str(settings.set('timeSeparatorMode', 'wobble'))) "
      "log(str(settings.set('scroll', 1))) "
      "log(str(settings.set('weekdayBar', 1)))");
  TEST_ASSERT_FALSE(logged("true"));
  TEST_ASSERT_EQUAL_INT(0, static_cast<int>(g_writes.size()));
}

static void test_set_cannot_inject_a_second_member() {
  run("log(str(settings.set('brightness\":1,\"autoBrightness', 1)))");
  TEST_ASSERT_TRUE(logged("false"));
  TEST_ASSERT_EQUAL_INT(0, static_cast<int>(g_writes.size()));
}

static void test_setting_the_current_value_queues_nothing() {
  g_set.brightness = 40;
  g_set.uppercase = true;
  g_set.gamma = 1.5f;
  run("log(str(settings.set('brightness', 40))) "
      "log(str(settings.set('uppercase', true))) "
      "log(str(settings.set('gamma', 1.5))) "
      "log(str(settings.set('timeSeparatorMode', 'PULSE'))) "
      "log(str(settings.set('timeColor', nil)))");
  TEST_ASSERT_FALSE(logged("false"));
  TEST_ASSERT_EQUAL_INT(0, static_cast<int>(g_writes.size()));
}

static void test_set_reports_a_refused_queue() {
  g_writeAccepts = false;
  run("log(str(settings.set('brightness', 40)))");
  TEST_ASSERT_TRUE(logged("false"));
  TEST_ASSERT_EQUAL_INT(1, static_cast<int>(g_writes.size()));
}

static void test_set_without_a_write_service_is_false_not_a_crash() {
  g_svc.setSettings = nullptr;
  run("log(str(settings.set('brightness', 40)))");
  TEST_ASSERT_TRUE(logged("false"));
}

static void test_apply_case_follows_the_device_setting() {
  g_set.uppercase = true;
  run("log(settings.apply_case('Zug 12'))");
  TEST_ASSERT_TRUE(logged("ZUG 12"));
  g_log.clear();
  g_set.uppercase = false;
  run("log(settings.apply_case('Zug 12'))");
  TEST_ASSERT_TRUE(logged("Zug 12"));
}

static void test_apply_case_takes_a_number_as_well() {
  g_set.uppercase = true;
  run("log(settings.apply_case(21))");
  TEST_ASSERT_TRUE(logged("21"));
}

static void test_no_settings_wired_reads_nil() {
  g_svc.settings = nullptr;
  run("log(str(settings.get('textColor'))) log('/') log(settings.apply_case('ab')) log('/') "
      "log(str(settings.set('brightness', 40)))");
  TEST_ASSERT_TRUE(logged("nil"));
  TEST_ASSERT_TRUE(logged("ab"));
  TEST_ASSERT_TRUE(logged("false"));
  TEST_ASSERT_EQUAL_INT(0, static_cast<int>(g_writes.size()));
}

static void test_null_settings_pointer_reads_nil() {
  g_svc.settings = []() -> const Settings* { return nullptr; };
  run("log(str(settings.get('uppercase'))) log('/') log(str(settings.set('uppercase', false)))");
  TEST_ASSERT_TRUE(logged("nil"));
  TEST_ASSERT_TRUE(logged("false"));
  TEST_ASSERT_EQUAL_INT(0, static_cast<int>(g_writes.size()));
}

static void test_sound_actions_reach_the_service() {
  run("sound.play('2') sound.rtttl('x:d=4,o=5,b=100:c') sound.stop()");
  TEST_ASSERT_EQUAL_INT(3, static_cast<int>(g_sounds.size()));
  TEST_ASSERT_EQUAL_INT(static_cast<int>(script::SoundAction::Play),
                        static_cast<int>(g_sounds[0].action));
  TEST_ASSERT_EQUAL_STRING("2", g_sounds[0].payload.c_str());
  TEST_ASSERT_EQUAL_INT(static_cast<int>(script::SoundAction::Rtttl),
                        static_cast<int>(g_sounds[1].action));
  TEST_ASSERT_EQUAL_STRING("x:d=4,o=5,b=100:c", g_sounds[1].payload.c_str());
  TEST_ASSERT_EQUAL_INT(static_cast<int>(script::SoundAction::Stop),
                        static_cast<int>(g_sounds[2].action));
  TEST_ASSERT_TRUE(g_sounds[2].payload.empty());
}

static void test_sound_reports_whether_it_was_accepted() {
  g_soundAccepts = false;
  run("log(str(sound.play('2')))");
  TEST_ASSERT_TRUE(logged("false"));
}

static void test_sound_without_service_is_false_not_a_crash() {
  g_svc.sound = nullptr;
  run("log(str(sound.play('2'))) sound.stop()");
  TEST_ASSERT_TRUE(logged("false"));
  TEST_ASSERT_EQUAL_INT(0, static_cast<int>(g_sounds.size()));
}

static void test_sound_playing_reports_the_service_state() {
  g_svc.soundPlaying = [] { return true; };
  run("log(str(sound.playing()))");
  TEST_ASSERT_TRUE(logged("true"));
  g_log.clear();
  g_svc.soundPlaying = [] { return false; };
  run("log(str(sound.playing()))");
  TEST_ASSERT_TRUE(logged("false"));
}

static void test_sound_playing_without_service_is_false() {
  g_svc.soundPlaying = nullptr;
  run("log(str(sound.playing()))");
  TEST_ASSERT_TRUE(logged("false"));
}

int main(int, char**) {
  UNITY_BEGIN();
  RUN_TEST(test_get_answers_the_api_keys);
  RUN_TEST(test_get_covers_every_field_kind);
  RUN_TEST(test_get_of_an_unknown_key_is_nil);
  RUN_TEST(test_unset_accent_colour_reads_nil);
  RUN_TEST(test_set_queues_the_canonical_patch);
  RUN_TEST(test_set_carries_every_value_shape);
  RUN_TEST(test_set_nil_clears_an_accent_colour);
  RUN_TEST(test_set_rejects_what_the_rest_api_rejects);
  RUN_TEST(test_set_cannot_inject_a_second_member);
  RUN_TEST(test_setting_the_current_value_queues_nothing);
  RUN_TEST(test_set_reports_a_refused_queue);
  RUN_TEST(test_set_without_a_write_service_is_false_not_a_crash);
  RUN_TEST(test_apply_case_follows_the_device_setting);
  RUN_TEST(test_apply_case_takes_a_number_as_well);
  RUN_TEST(test_no_settings_wired_reads_nil);
  RUN_TEST(test_null_settings_pointer_reads_nil);
  RUN_TEST(test_sound_actions_reach_the_service);
  RUN_TEST(test_sound_reports_whether_it_was_accepted);
  RUN_TEST(test_sound_without_service_is_false_not_a_crash);
  RUN_TEST(test_sound_playing_reports_the_service_state);
  RUN_TEST(test_sound_playing_without_service_is_false);
  return UNITY_END();
}
