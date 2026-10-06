#include <unity.h>
#include "../ScriptApplication.h"

#include <string>
#include <vector>

#include "core/Settings.h"
#include "core/apps/IApp.h"
#include "core/script/BerryVM.h"
#include "core/script/ScriptBindings.h"
#include "core/script/ScriptServices.h"

using namespace awtrix;

static script::ScriptServices g_svc;
static awtrix::test::ScriptApplication application;
static Settings g_set;
static std::string g_log;
static std::vector<std::string> g_writes;
static bool g_writeAccepts = true;

struct SoundCall {
  script::SoundAction action;
  std::string json;
  std::string script;
};
static std::vector<SoundCall> g_sounds;
static bool g_canPlay = true;
static int g_caps = 0;

void setUp() {
  g_set = Settings{};
  g_log.clear();
  g_writes.clear();
  g_writeAccepts = true;
  g_sounds.clear();
  g_canPlay = true;
  g_caps = 0;

  g_svc = script::ScriptServices{};
  application = {};
  g_svc.application = &application;
  application.settingsFn = [] { return &g_set; };
  application.setSettingsFn = [](const std::string& json) {
    g_writes.push_back(json);
    return g_writeAccepts;
  };
  // A mistake in the sound object answers -1 with its reason; a clock that can play none of it 0.
  application.soundFn = [](script::SoundAction a, const std::string& json, const std::string& owner,
                   std::string& error) {
    if (json.find("broken") != std::string::npos || json.find("mp3") != std::string::npos) {
      error = "rtttl: missing ':'";
      return -1;
    }
    if (!g_canPlay) return 0;
    g_sounds.push_back({a, json, owner});
    return 1;
  };
  application.soundCapsFn = [] { return g_caps; };
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

// The error a draw() that is meant to fail ends with.
static std::string runFails(const char* body) {
  script::BerryVM vm;
  std::string err;
  TEST_ASSERT_TRUE_MESSAGE(script::installBindings(vm, err), err.c_str());
  const std::string src = std::string("def draw() ") + body + " end";
  TEST_ASSERT_TRUE_MESSAGE(vm.load(src.c_str()), vm.lastError().c_str());
  script::BindingScope s(nullptr, nullptr, "T");
  TEST_ASSERT_FALSE_MESSAGE(vm.call("draw"), "draw() was meant to fail");
  return vm.lastError();
}

static bool runBool(const char* expression) {
  g_log.clear();
  run((std::string("log(str(") + expression + "))").c_str());
  const bool answer = logged("true");
  TEST_ASSERT_TRUE_MESSAGE(answer != logged("false"), g_log.c_str());
  g_log.clear();
  return answer;
}

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
  application.setSettingsFn = nullptr;
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
  application.settingsFn = nullptr;
  run("log(str(settings.get('textColor'))) log('/') log(settings.apply_case('ab')) log('/') "
      "log(str(settings.set('brightness', 40)))");
  TEST_ASSERT_TRUE(logged("nil"));
  TEST_ASSERT_TRUE(logged("ab"));
  TEST_ASSERT_TRUE(logged("false"));
  TEST_ASSERT_EQUAL_INT(0, static_cast<int>(g_writes.size()));
}

static void test_null_settings_pointer_reads_nil() {
  application.settingsFn = []() -> const Settings* { return nullptr; };
  run("log(str(settings.get('uppercase'))) log('/') log(str(settings.set('uppercase', false)))");
  TEST_ASSERT_TRUE(logged("nil"));
  TEST_ASSERT_TRUE(logged("false"));
  TEST_ASSERT_EQUAL_INT(0, static_cast<int>(g_writes.size()));
}

static void test_play_sends_the_sound_object() {
  run("sound.play('ding') sound.play({'file':'boost'}) "
      "sound.play({'song':'bpm 90','loop':true})");
  TEST_ASSERT_EQUAL_INT(3, static_cast<int>(g_sounds.size()));
  TEST_ASSERT_EQUAL_INT((int)script::SoundAction::Play, (int)g_sounds[0].action);
  TEST_ASSERT_EQUAL_STRING("\"ding\"", g_sounds[0].json.c_str());
  TEST_ASSERT_EQUAL_INT((int)script::SoundAction::Play, (int)g_sounds[1].action);
  TEST_ASSERT_EQUAL_STRING("{\"file\":\"boost\"}", g_sounds[1].json.c_str());
  TEST_ASSERT_EQUAL_STRING("T", g_sounds[1].script.c_str());
  TEST_ASSERT_EQUAL_INT((int)script::SoundAction::Play, (int)g_sounds[2].action);
  TEST_ASSERT_TRUE(g_sounds[2].json.find("\"loop\":true") != std::string::npos);
}

// The script that is named is the one that asked, never an argument it passes.
static void test_sound_names_the_script_that_asked() {
  run("sound.play('boost') sound.play(['ding', {'track':3}]) sound.stop()");
  TEST_ASSERT_EQUAL_INT(3, static_cast<int>(g_sounds.size()));
  for (const SoundCall& call : g_sounds) TEST_ASSERT_EQUAL_STRING("T", call.script.c_str());
}

// A malformed sound object raises at the script's own line; hardware never raises.
static void test_a_bad_sound_object_raises() {
  std::string error = runFails("sound.play({'mp3':'x'})");
  TEST_ASSERT_TRUE_MESSAGE(error.find("value_error") != std::string::npos, error.c_str());
  error = runFails("sound.play({'rtttl':'broken'})");
  TEST_ASSERT_TRUE_MESSAGE(error.find("value_error") != std::string::npos, error.c_str());
  TEST_ASSERT_TRUE_MESSAGE(error.find("rtttl: missing ':'") != std::string::npos, error.c_str());
  error = runFails("sound.stop('all')");
  TEST_ASSERT_TRUE_MESSAGE(error.find("value_error") != std::string::npos, error.c_str());
  TEST_ASSERT_EQUAL_INT(0, static_cast<int>(g_sounds.size()));
}

static void test_play_is_false_when_nothing_here_can_play_it() {
  g_canPlay = false;
  TEST_ASSERT_TRUE(runBool("sound.play({'speech':'Hi'})") == false);
  g_canPlay = true;
  TEST_ASSERT_TRUE(runBool("sound.play({'speech':'Hi'})"));
}

static void test_stop_sends_the_scripts_own_stop() {
  run("sound.stop() sound.stop('loop')");
  TEST_ASSERT_EQUAL_INT(2, static_cast<int>(g_sounds.size()));
  TEST_ASSERT_EQUAL_INT((int)script::SoundAction::Stop, (int)g_sounds[0].action);
  TEST_ASSERT_EQUAL_INT((int)script::SoundAction::StopMusic, (int)g_sounds[1].action);
  TEST_ASSERT_TRUE(g_sounds[0].json.empty());
}

static void test_sound_without_service_is_false_not_a_crash() {
  application.soundFn = nullptr;
  run("log(str(sound.play('2'))) sound.stop()");
  TEST_ASSERT_TRUE(logged("false"));
  TEST_ASSERT_EQUAL_INT(0, static_cast<int>(g_sounds.size()));
}

static void test_sound_playing_reports_the_service_state() {
  application.soundPlayingFn = [] { return true; };
  run("log(str(sound.playing()))");
  TEST_ASSERT_TRUE(logged("true"));
  g_log.clear();
  application.soundPlayingFn = [] { return false; };
  run("log(str(sound.playing()))");
  TEST_ASSERT_TRUE(logged("false"));
}

static void test_sound_playing_without_service_is_false() {
  application.soundPlayingFn = nullptr;
  run("log(str(sound.playing()))");
  TEST_ASSERT_TRUE(logged("false"));
}

static void test_can_reports_the_capability_flags() {
  g_caps = 1 | 4 | 128;
  TEST_ASSERT_TRUE(runBool("sound.can('mp3')"));
  TEST_ASSERT_TRUE(runBool("sound.can('song')"));
  TEST_ASSERT_TRUE(runBool("sound.can('effect')"));
  TEST_ASSERT_FALSE(runBool("sound.can('speech')"));
  TEST_ASSERT_FALSE(runBool("sound.can('hologram')"));
  TEST_ASSERT_TRUE(runBool("sound.can()['effect']"));
  TEST_ASSERT_FALSE(runBool("sound.can()['clip']"));
  g_caps = 2 | 8 | 16 | 32 | 64 | 256;
  for (const char* flag : {"rtttl", "speech", "track", "radio", "url", "clip"}) {
    const std::string call = std::string("sound.can('") + flag + "')";
    TEST_ASSERT_TRUE_MESSAGE(runBool(call.c_str()), flag);
  }
  application.soundCapsFn = nullptr;
  TEST_ASSERT_FALSE(runBool("sound.can('rtttl')"));
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
  RUN_TEST(test_play_sends_the_sound_object);
  RUN_TEST(test_sound_names_the_script_that_asked);
  RUN_TEST(test_a_bad_sound_object_raises);
  RUN_TEST(test_play_is_false_when_nothing_here_can_play_it);
  RUN_TEST(test_stop_sends_the_scripts_own_stop);
  RUN_TEST(test_sound_without_service_is_false_not_a_crash);
  RUN_TEST(test_sound_playing_reports_the_service_state);
  RUN_TEST(test_sound_playing_without_service_is_false);
  RUN_TEST(test_can_reports_the_capability_flags);
  return UNITY_END();
}
