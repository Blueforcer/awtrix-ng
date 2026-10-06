#include <unity.h>

#include <string>
#include <string_view>

#include "core/CoreEngine.h"
#include "core/api/ApiRouter.h"
#include "core/api/JsonReader.h"
#include "core/sound/AudioRouter.h"

using namespace awtrix;

void setUp() {}
void tearDown() {}

static int ct(CommandType t) { return static_cast<int>(t); }
static int ro(api::RouteOutcome o) { return static_cast<int>(o); }

// routeMqtt may take the payload it is handed, so each call routes its own copy.
static api::RouteOutcome routeMqttCopy(const std::string& suffix, std::string payload, Command& c,
                                       std::string& result) {
  return api::routeMqtt(suffix, payload, c, result);
}

static Command routed(const char* method, const char* path, const char* body = "") {
  Command c;
  api::HttpResult imm;
  TEST_ASSERT_EQUAL_INT_MESSAGE(ro(api::RouteOutcome::Routed),
                                ro(api::routeHttp(method, path, body, c, imm)), path);
  return c;
}

// What a caller gets back, as every transport answers: the route's own reply, or the command run
// at once on a clock without sound outputs.
struct Clock {
  struct Display : IDisplayService {
    void sendScreen() override {}
  } display;
  struct System : ISystemService {
    void reboot() override {}
    void sleep(uint64_t) override {}
    void factoryReset() override {}
    void resetSettings() override {}
  } system;
  sound::AudioRouter audio;
  CoreEngine engine{audio, display, system};

  api::HttpResult http(const char* method, const char* path, const char* body) {
    Command c;
    api::HttpResult imm;
    if (api::routeHttp(method, path, body, c, imm) != api::RouteOutcome::Routed) return imm;
    const DispatchResult r = engine.execute(c);
    return api::httpResponse(c, r, engine.lastDetail());
  }
  std::string mqtt(const std::string& suffix, std::string payload) {
    Command c;
    std::string result;
    if (api::routeMqtt(suffix, payload, c, result) != api::RouteOutcome::Routed) return result;
    const DispatchResult r = engine.execute(c);
    return api::mqttResult(r, engine.lastDetail());
  }
};


static void test_http_radio_routes() {
  // A station keeps its payload whole: the dispatch reads the name, position or address from it.
  Command c = routed("POST", "/api/v1/audio/play", "{\"station\":\"SWR3\"}");
  TEST_ASSERT_EQUAL_INT(ct(CommandType::PlayAudio), ct(c.type));
  TEST_ASSERT_EQUAL_STRING("{\"station\":\"SWR3\"}", c.payload.c_str());

  c = routed("POST", "/api/v1/audio/play", "{\"station\":\"http://s/x\"}");
  TEST_ASSERT_EQUAL_INT(ct(CommandType::PlayAudio), ct(c.type));

  c = routed("POST", "/api/v1/audio/stop");
  TEST_ASSERT_EQUAL_INT(ct(CommandType::StopAudio), ct(c.type));

  c = routed("PUT", "/api/v1/audio/stations", "{\"stations\":[]}");
  TEST_ASSERT_EQUAL_INT(ct(CommandType::SetRadioStations), ct(c.type));
}

static void test_http_radio_read_falls_through_to_the_transport() {
  Command c;
  api::HttpResult imm;
  TEST_ASSERT_EQUAL_INT(ro(api::RouteOutcome::NoMatch),
                        ro(api::routeHttp("GET", "/api/v1/audio", "", c, imm)));
}

static void test_http_radio_wrong_methods_are_405_not_404() {
  Command c;
  api::HttpResult imm;
  TEST_ASSERT_EQUAL_INT(ro(api::RouteOutcome::Respond),
                        ro(api::routeHttp("POST", "/api/v1/audio", "", c, imm)));
  TEST_ASSERT_EQUAL_INT(405, imm.status);
  TEST_ASSERT_EQUAL_INT(ro(api::RouteOutcome::Respond),
                        ro(api::routeHttp("GET", "/api/v1/audio/stop", "", c, imm)));
  TEST_ASSERT_EQUAL_INT(405, imm.status);
  TEST_ASSERT_EQUAL_INT(ro(api::RouteOutcome::Respond),
                        ro(api::routeHttp("POST", "/api/v1/audio/stations", "", c, imm)));
  TEST_ASSERT_EQUAL_INT(405, imm.status);
}

static void test_http_radio_play_needs_a_body() {
  Clock clock;
  TEST_ASSERT_EQUAL_INT(422, clock.http("POST", "/api/v1/audio/play", "{}").status);
}

static void test_mqtt_radio_ops() {
  Command c;
  std::string result;
  TEST_ASSERT_EQUAL_INT(ro(api::RouteOutcome::Routed),
                        ro(routeMqttCopy("cmd/audio/play", "{\"station\":0}", c, result)));
  TEST_ASSERT_EQUAL_INT(ct(CommandType::PlayAudio), ct(c.type));
  TEST_ASSERT_EQUAL_INT((int)Source::Mqtt, (int)c.source);

  TEST_ASSERT_EQUAL_INT(ro(api::RouteOutcome::Routed),
                        ro(routeMqttCopy("cmd/audio/stop", "", c, result)));
  TEST_ASSERT_EQUAL_INT(ct(CommandType::StopAudio), ct(c.type));

  TEST_ASSERT_EQUAL_INT(ro(api::RouteOutcome::Routed),
                        ro(routeMqttCopy("cmd/audio/stations", "[]", c, result)));
  TEST_ASSERT_EQUAL_INT(ct(CommandType::SetRadioStations), ct(c.type));
}

static void test_http_notifications() {
  Command c = routed("POST", "/api/v1/notifications", "{\"text\":\"x\"}");
  TEST_ASSERT_EQUAL_INT(ct(CommandType::Notify), ct(c.type));
  TEST_ASSERT_EQUAL_STRING("{\"text\":\"x\"}", c.payload.c_str());
  TEST_ASSERT_EQUAL_INT((int)Source::Http, (int)c.source);

  c = routed("DELETE", "/api/v1/notifications/active");
  TEST_ASSERT_EQUAL_INT(ct(CommandType::DismissNotify), ct(c.type));
}

static void test_http_pushed_apps_name_from_path() {
  Command c = routed("PUT", "/api/v1/apps/pushed/weather", "{\"text\":\"x\"}");
  TEST_ASSERT_EQUAL_INT(ct(CommandType::SetPushedApp), ct(c.type));
  TEST_ASSERT_EQUAL_STRING("weather", c.name.c_str());
  TEST_ASSERT_EQUAL_STRING("{\"text\":\"x\"}", c.payload.c_str());
  TEST_ASSERT_FALSE(c.clear);

  Command r;
  api::HttpResult imm;
  TEST_ASSERT_EQUAL_INT(ro(api::RouteOutcome::Respond),
                        ro(api::routeHttp("DELETE", "/api/v1/apps/pushed/weather", "", r, imm)));
  TEST_ASSERT_EQUAL_INT(405, imm.status);

  TEST_ASSERT_EQUAL_INT(ro(api::RouteOutcome::Respond),
                        ro(api::routeHttp("PUT", "/api/v1/apps/pushed/weather", "{}", r, imm)));
  TEST_ASSERT_EQUAL_INT(422, imm.status);
  TEST_ASSERT_TRUE(imm.body.find("\"code\":\"validationFailed\"") != std::string::npos);
  TEST_ASSERT_EQUAL_INT(ro(api::RouteOutcome::Respond),
                        ro(api::routeHttp("PUT", "/api/v1/apps/pushed/weather", "", r, imm)));
  TEST_ASSERT_EQUAL_INT(422, imm.status);
  TEST_ASSERT_EQUAL_INT(ro(api::RouteOutcome::Respond),
                        ro(api::routeHttp("PUT", "/api/v1/apps/pushed/weather", "{ \n}", r, imm)));
  TEST_ASSERT_EQUAL_INT(422, imm.status);

  Command d = routed("DELETE", "/api/v1/apps/weather", "");
  TEST_ASSERT_EQUAL_INT(ct(CommandType::DeleteApp), ct(d.type));
  TEST_ASSERT_EQUAL_STRING("weather", d.name.c_str());
  TEST_ASSERT_TRUE(d.clear);
}

static void test_http_pushed_app_name_is_validated() {
  Command c;
  api::HttpResult imm;
  TEST_ASSERT_EQUAL_INT(
      ro(api::RouteOutcome::Respond),
      ro(api::routeHttp("PUT", "/api/v1/apps/pushed/../../ICONS/trav2", "{\"text\":\"x\"}", c,
                        imm)));
  TEST_ASSERT_EQUAL_INT(400, imm.status);
  TEST_ASSERT_TRUE(imm.body.find("invalidName") != std::string::npos);
  TEST_ASSERT_TRUE(imm.body.find("\"field\":\"name\"") != std::string::npos);

  TEST_ASSERT_EQUAL_INT(
      ro(api::RouteOutcome::Respond),
      ro(api::routeHttp("PUT", "/api/v1/apps/pushed/a%2Fb", "{\"text\":\"x\"}", c, imm)));
  TEST_ASSERT_EQUAL_INT(400, imm.status);

  const std::string tooLong(33, 'a');
  TEST_ASSERT_EQUAL_INT(
      ro(api::RouteOutcome::Respond),
      ro(api::routeHttp("PUT", ("/api/v1/apps/pushed/" + tooLong).c_str(), "{\"text\":\"x\"}", c,
                        imm)));
  TEST_ASSERT_EQUAL_INT(400, imm.status);
}

static void test_http_settings_methods() {
  Command c = routed("PATCH", "/api/v1/settings", "{\"brightness\":10}");
  TEST_ASSERT_EQUAL_INT(ct(CommandType::SetSettings), ct(c.type));

  Command r;
  api::HttpResult imm;
  TEST_ASSERT_EQUAL_INT(ro(api::RouteOutcome::NoMatch),
                        ro(api::routeHttp("GET", "/api/v1/settings", "", r, imm)));

  TEST_ASSERT_EQUAL_INT(ro(api::RouteOutcome::Respond),
                        ro(api::routeHttp("POST", "/api/v1/settings", "{}", r, imm)));
  TEST_ASSERT_EQUAL_INT(405, imm.status);

  c = routed("POST", "/api/v1/settings/reset");
  TEST_ASSERT_EQUAL_INT(ct(CommandType::ResetSettings), ct(c.type));
}

static void test_http_display() {
  Command c = routed("PATCH", "/api/v1/display", "{\"power\":false}");
  TEST_ASSERT_EQUAL_INT(ct(CommandType::SetDisplay), ct(c.type));

  c = routed("PUT", "/api/v1/display/moodlight", "{\"color\":\"#FF0000\"}");
  TEST_ASSERT_EQUAL_INT(ct(CommandType::Moodlight), ct(c.type));

  c = routed("DELETE", "/api/v1/display/moodlight");
  TEST_ASSERT_EQUAL_INT(ct(CommandType::Moodlight), ct(c.type));
  TEST_ASSERT_TRUE(c.clear);

  Command r;
  api::HttpResult imm;
  TEST_ASSERT_EQUAL_INT(ro(api::RouteOutcome::Respond),
                        ro(api::routeHttp("PUT", "/api/v1/display/moodlight", "{}", r, imm)));
  TEST_ASSERT_EQUAL_INT(422, imm.status);
  TEST_ASSERT_EQUAL_INT(ro(api::RouteOutcome::Respond),
                        ro(api::routeHttp("PUT", "/api/v1/display/moodlight", "", r, imm)));
  TEST_ASSERT_EQUAL_INT(422, imm.status);
  TEST_ASSERT_EQUAL_INT(ro(api::RouteOutcome::Respond),
                        ro(api::routeHttp("PUT", "/api/v1/display/moodlight", "{ }", r, imm)));
  TEST_ASSERT_EQUAL_INT(422, imm.status);
}

static void test_http_apps() {
  Command c = routed("PUT", "/api/v1/apps/active", "{\"name\":\"Time\",\"fast\":true}");
  TEST_ASSERT_EQUAL_INT(ct(CommandType::SwitchApp), ct(c.type));

  c = routed("POST", "/api/v1/apps/next");
  TEST_ASSERT_EQUAL_INT(ct(CommandType::NextApp), ct(c.type));
  c = routed("POST", "/api/v1/apps/previous");
  TEST_ASSERT_EQUAL_INT(ct(CommandType::PreviousApp), ct(c.type));

  c = routed("PUT", "/api/v1/apps/order", "{\"order\":[\"Time\",\"Date\"]}");
  TEST_ASSERT_EQUAL_INT(ct(CommandType::SetAppOrder), ct(c.type));

  Command r;
  api::HttpResult imm;
  TEST_ASSERT_EQUAL_INT(ro(api::RouteOutcome::NoMatch),
                        ro(api::routeHttp("GET", "/api/v1/apps", "", r, imm)));
}

static void test_http_app_switch_names_the_app_in_the_path() {
  Command c = routed("PUT", "/api/v1/apps/Super-Alarm-Clock/enabled", "false");
  TEST_ASSERT_EQUAL_INT(ct(CommandType::SetAppEnabled), ct(c.type));
  TEST_ASSERT_EQUAL_STRING("Super-Alarm-Clock", c.name.c_str());
  TEST_ASSERT_EQUAL_STRING("false", c.payload.c_str());

  Command r;
  api::HttpResult imm;
  TEST_ASSERT_EQUAL_INT(ro(api::RouteOutcome::Respond),
                        ro(api::routeHttp("GET", "/api/v1/apps/Status/enabled", "", r, imm)));
  TEST_ASSERT_EQUAL_INT(405, imm.status);
  TEST_ASSERT_EQUAL_INT(ro(api::RouteOutcome::Respond),
                        ro(api::routeHttp("PUT", "/api/v1/apps/order/enabled", "true", r, imm)));
  TEST_ASSERT_EQUAL_INT(400, imm.status);
}

static void test_mqtt_app_switch_names_the_app_in_the_topic() {
  Command c;
  std::string result;
  TEST_ASSERT_EQUAL_INT(ro(api::RouteOutcome::Routed),
                        ro(routeMqttCopy("cmd/apps/Status/enabled", "true", c, result)));
  TEST_ASSERT_EQUAL_INT(ct(CommandType::SetAppEnabled), ct(c.type));
  TEST_ASSERT_EQUAL_STRING("Status", c.name.c_str());
  TEST_ASSERT_EQUAL_STRING("true", c.payload.c_str());
  TEST_ASSERT_TRUE(api::isResultEcho("cmd/apps/Status/enabled/result"));
  TEST_ASSERT_EQUAL_INT(ro(api::RouteOutcome::Respond),
                        ro(routeMqttCopy("cmd/apps/order/enabled", "true", c, result)));
  TEST_ASSERT_TRUE(result.find("invalidName") != std::string::npos);
}

static void test_app_switch_answers_over_http_and_mqtt() {
  Clock clock;
  api::HttpResult res = clock.http("PUT", "/api/v1/apps/Date/enabled", "false");
  TEST_ASSERT_EQUAL_INT(200, res.status);
  TEST_ASSERT_EQUAL_STRING("{\"ok\":true}", res.body.c_str());
  TEST_ASSERT_FALSE(clock.engine.isEnabled("Date"));

  res = clock.http("PUT", "/api/v1/apps/Date/enabled", "off");
  TEST_ASSERT_EQUAL_INT(422, res.status);
  TEST_ASSERT_TRUE(res.body.find("must be true or false") != std::string::npos);

  TEST_ASSERT_EQUAL_STRING("{\"ok\":true}", clock.mqtt("cmd/apps/Date/enabled", "true").c_str());
  TEST_ASSERT_TRUE(clock.engine.isEnabled("Date"));
}

static void test_http_indicators() {
  Command c = routed("PUT", "/api/v1/indicators/2", "{\"color\":\"#00FF00\"}");
  TEST_ASSERT_EQUAL_INT(ct(CommandType::SetIndicator), ct(c.type));
  TEST_ASSERT_EQUAL_INT(2, c.arg);

  c = routed("DELETE", "/api/v1/indicators/3");
  TEST_ASSERT_EQUAL_INT(3, c.arg);
  TEST_ASSERT_TRUE(c.clear);

  Command r;
  api::HttpResult imm;
  TEST_ASSERT_EQUAL_INT(ro(api::RouteOutcome::Respond),
                        ro(api::routeHttp("PUT", "/api/v1/indicators/4", "{}", r, imm)));
  TEST_ASSERT_EQUAL_INT(404, imm.status);

  TEST_ASSERT_EQUAL_INT(ro(api::RouteOutcome::Respond),
                        ro(api::routeHttp("PUT", "/api/v1/indicators/1", "{}", r, imm)));
  TEST_ASSERT_EQUAL_INT(422, imm.status);
  TEST_ASSERT_EQUAL_INT(ro(api::RouteOutcome::Respond),
                        ro(api::routeHttp("PUT", "/api/v1/indicators/1", "", r, imm)));
  TEST_ASSERT_EQUAL_INT(422, imm.status);
  TEST_ASSERT_EQUAL_INT(ro(api::RouteOutcome::Respond),
                        ro(api::routeHttp("PUT", "/api/v1/indicators/1", "{ }", r, imm)));
  TEST_ASSERT_EQUAL_INT(422, imm.status);
}

static void test_http_audio_clip() {
  Command command;
  api::HttpResult response;
  for (const char* method : {"POST", "GET", "PUT", "DELETE"})
    TEST_ASSERT_EQUAL_INT(ro(api::RouteOutcome::NoMatch),
        ro(api::routeHttp(method, "/api/v1/audio/clip", "recording", command, response)));
}

// The sound object goes to the dispatcher as sent; its mistakes answer before dispatch.
static void test_http_play_routes_the_sound_object() {
  Command c = routed("POST", "/api/v1/audio/play", "{\"file\":\"ding\",\"loop\":true}");
  TEST_ASSERT_EQUAL_INT(ct(CommandType::PlayAudio), ct(c.type));
  TEST_ASSERT_EQUAL_STRING("{\"file\":\"ding\",\"loop\":true}", c.payload.c_str());
  TEST_ASSERT_EQUAL_STRING("", c.name.c_str());
  TEST_ASSERT_EQUAL_INT((int)sound::PlayAs::Once, c.arg);
  c = routed("POST", "/api/v1/audio/play", "[{\"speech\":\"Hi\"},\"ding\"]");
  TEST_ASSERT_EQUAL_INT(ct(CommandType::PlayAudio), ct(c.type));
  c = routed("POST", "/api/v1/audio/play", "{\"station\":2}");
  TEST_ASSERT_EQUAL_INT(ct(CommandType::PlayAudio), ct(c.type));
}

static void test_http_play_rejects_with_the_parsers_field() {
  struct Case {
    const char* body;
    const char* field;
  };
  for (const Case& k : {Case{"{\"mp3\":\"ding\"}", "mp3"}, Case{"{\"track\":0}", "track"},
                        Case{"{\"file\":\"a b\"}", "file"},
                        Case{"{\"song\":\"x\",\"loop\":true,\"nextBar\":true}", "nextBar"},
                        Case{"[\"a\",{\"station\":\"x\"}]", "[1].station"}}) {
    Clock clock;
    const api::HttpResult imm = clock.http("POST", "/api/v1/audio/play", k.body);
    TEST_ASSERT_EQUAL_INT_MESSAGE(422, imm.status, k.body);
    TEST_ASSERT_TRUE_MESSAGE(imm.body.find("validationFailed") != std::string::npos, k.body);
    const std::string want = std::string("\"field\":\"") + k.field + "\"";
    TEST_ASSERT_TRUE_MESSAGE(imm.body.find(want) != std::string::npos, k.body);
  }
}

static void test_http_play_rejects_malformed_json_with_400() {
  Command r;
  api::HttpResult imm;
  api::routeHttp("POST", "/api/v1/audio/play", "{\"file\":", r, imm);
  TEST_ASSERT_EQUAL_INT(400, imm.status);
}

static void test_http_stop_takes_a_group() {
  Command c = routed("POST", "/api/v1/audio/stop", "");
  TEST_ASSERT_EQUAL_INT((int)sound::Stop::All, c.arg);
  c = routed("POST", "/api/v1/audio/stop", "{}");
  TEST_ASSERT_EQUAL_INT((int)sound::Stop::All, c.arg);
  c = routed("POST", "/api/v1/audio/stop", "{\"group\":\"alert\"}");
  TEST_ASSERT_EQUAL_INT((int)sound::Stop::Alert, c.arg);
  c = routed("POST", "/api/v1/audio/stop", "{\"group\":\"app\"}");
  TEST_ASSERT_EQUAL_INT((int)sound::Stop::App, c.arg);
  c = routed("POST", "/api/v1/audio/stop", "{\"group\":\"radio\"}");
  TEST_ASSERT_EQUAL_INT((int)sound::Stop::Radio, c.arg);
  for (const char* body : {"{\"group\":\"loop\"}", "{\"scope\":\"all\"}", "{\"group\":1}"}) {
    Command r;
    api::HttpResult imm;
    api::routeHttp("POST", "/api/v1/audio/stop", body, r, imm);
    TEST_ASSERT_EQUAL_INT_MESSAGE(422, imm.status, body);
  }
  Command r;
  api::HttpResult imm;
  api::routeHttp("POST", "/api/v1/audio/stop", "{\"group\":\"loop\"}", r, imm);
  TEST_ASSERT_TRUE(imm.body.find("must be alert, app or radio") != std::string::npos);
}

static void test_mqtt_audio_shares_the_http_rules() {
  Command c;
  std::string res;
  TEST_ASSERT_EQUAL_INT(ro(api::RouteOutcome::Routed),
                        ro(routeMqttCopy("cmd/audio/play", "\"ding\"", c, res)));
  TEST_ASSERT_EQUAL_INT(ct(CommandType::PlayAudio), ct(c.type));
  TEST_ASSERT_EQUAL_INT(ro(api::RouteOutcome::Routed),
                        ro(routeMqttCopy("cmd/audio/stop", "{\"group\":\"radio\"}", c, res)));
  TEST_ASSERT_EQUAL_INT((int)sound::Stop::Radio, c.arg);
  TEST_ASSERT_EQUAL_INT(ro(api::RouteOutcome::Respond),
                        ro(routeMqttCopy("cmd/audio/stop", "{\"group\":\"x\"}", c, res)));
  TEST_ASSERT_TRUE(res.find("\"ok\":false") != std::string::npos);
}

// The builtin sound key is refused.
static void test_http_sounds_play_no_longer_knows_builtin() {
  Clock clock;
  TEST_ASSERT_EQUAL_INT(422,
                        clock.http("POST", "/api/v1/audio/play", "{\"builtin\":\"r2d2\"}").status);
}

static void test_http_sounds_stop() {
  Command c = routed("POST", "/api/v1/audio/stop");
  TEST_ASSERT_EQUAL_INT(ct(CommandType::StopAudio), ct(c.type));
  TEST_ASSERT_EQUAL_INT((int)sound::Stop::All, c.arg);

  Command r;
  api::HttpResult imm;
  TEST_ASSERT_EQUAL_INT(ro(api::RouteOutcome::Respond),
                        ro(api::routeHttp("GET", "/api/v1/audio/stop", "", r, imm)));
  TEST_ASSERT_EQUAL_INT(405, imm.status);
}

static void test_mqtt_sounds_share_the_http_validation() {
  Command c;
  std::string res;
  TEST_ASSERT_EQUAL_INT(ro(api::RouteOutcome::Routed),
                        ro(routeMqttCopy("cmd/audio/stop", "", c, res)));
  TEST_ASSERT_EQUAL_INT(ct(CommandType::StopAudio), ct(c.type));

  // The same rules reach MQTT senders as a result payload, not only HTTP callers.
  Clock clock;
  res = clock.mqtt("cmd/audio/play", "{\"file\":\"a\",\"rtttl\":\"b\"}");
  TEST_ASSERT_TRUE(res.find("\"ok\":false") != std::string::npos);
  TEST_ASSERT_TRUE(res.find("validationFailed") != std::string::npos);
  TEST_ASSERT_TRUE(res.find("\"field\":\"file\"") != std::string::npos);
}

static void test_http_device_actions() {
  TEST_ASSERT_EQUAL_INT(ct(CommandType::Reboot), ct(routed("POST", "/api/v1/device/reboot").type));
  TEST_ASSERT_EQUAL_INT(ct(CommandType::Sleep),
                        ct(routed("POST", "/api/v1/device/sleep", "{\"durationMs\":1000}").type));
  TEST_ASSERT_EQUAL_INT(ct(CommandType::FactoryReset),
                        ct(routed("POST", "/api/v1/device/factory-reset").type));
}

static void test_http_reads_and_unknown_no_match() {
  Command r;
  api::HttpResult imm;
  TEST_ASSERT_EQUAL_INT(ro(api::RouteOutcome::NoMatch),
                        ro(api::routeHttp("GET", "/api/v1/device", "", r, imm)));
  TEST_ASSERT_EQUAL_INT(ro(api::RouteOutcome::NoMatch),
                        ro(api::routeHttp("GET", "/api/v1/capabilities", "", r, imm)));
  TEST_ASSERT_EQUAL_INT(ro(api::RouteOutcome::NoMatch),
                        ro(api::routeHttp("POST", "/api/v1/nope", "", r, imm)));
  TEST_ASSERT_EQUAL_INT(ro(api::RouteOutcome::NoMatch),
                        ro(api::routeHttp("POST", "/api/notify", "{}", r, imm)));
}

static void test_http_get_only_reads_reject_other_methods() {
  const char* paths[] = {"/api/v1/device",           "/api/v1/display/screen",
                         "/api/v1/capabilities",     "/api/v1/version",
                         "/version",                 "/api/v1/system/wifi-scan",
                         "/api/v1/logs"};
  Command r;
  api::HttpResult imm;
  for (const char* p : paths) {
    TEST_ASSERT_EQUAL_INT_MESSAGE(ro(api::RouteOutcome::NoMatch),
                                  ro(api::routeHttp("GET", p, "", r, imm)), p);
    TEST_ASSERT_EQUAL_INT_MESSAGE(ro(api::RouteOutcome::Respond),
                                  ro(api::routeHttp("POST", p, "", r, imm)), p);
    TEST_ASSERT_EQUAL_INT_MESSAGE(405, imm.status, p);
    TEST_ASSERT_TRUE_MESSAGE(imm.body.find("methodNotAllowed") != std::string::npos, p);
  }
}


static void test_http_shared_state_is_read_only() {
  Command r;
  api::HttpResult imm;
  TEST_ASSERT_EQUAL_INT(ro(api::RouteOutcome::NoMatch),
                        ro(api::routeHttp("GET", "/api/v1/scripts/shared", "", r, imm)));
  TEST_ASSERT_EQUAL_INT(ro(api::RouteOutcome::Respond),
                        ro(api::routeHttp("PUT", "/api/v1/scripts/shared", "{}", r, imm)));
  TEST_ASSERT_EQUAL_INT(405, imm.status);
}

static void test_app_name_validation() {
  TEST_ASSERT_TRUE(api::isValidAppName("Weather"));
  TEST_ASSERT_TRUE(api::isValidAppName("a"));
  TEST_ASSERT_TRUE(api::isValidAppName("my_script-2"));
  TEST_ASSERT_TRUE(api::isValidAppName("01234567890123456789012345678901"));
  TEST_ASSERT_FALSE(api::isValidAppName(""));
  TEST_ASSERT_FALSE(api::isValidAppName("012345678901234567890123456789012"));
  TEST_ASSERT_FALSE(api::isValidAppName("../x"));
  TEST_ASSERT_FALSE(api::isValidAppName("a/b"));
  TEST_ASSERT_FALSE(api::isValidAppName("a.ax"));
  TEST_ASSERT_FALSE(api::isValidAppName("a b"));
}

static void test_names_of_fixed_app_routes_are_refused() {
  for (const char* name : {"active", "next", "previous", "order"}) {
    TEST_ASSERT_FALSE_MESSAGE(api::isValidAppName(name), name);
    Command r;
    api::HttpResult imm;
    TEST_ASSERT_NOT_EQUAL(ro(api::RouteOutcome::Routed),
                          ro(api::routeHttp("PUT", std::string("/api/v1/apps/pushed/") + name,
                                            "{\"text\":\"x\"}", r, imm)));
    TEST_ASSERT_EQUAL_INT(400, imm.status);
    std::string result;
    TEST_ASSERT_EQUAL_INT(ro(api::RouteOutcome::Respond),
                          ro(routeMqttCopy(std::string("cmd/apps/pushed/") + name, "{\"text\":\"x\"}",
                                           r, result)));
    TEST_ASSERT_TRUE(result.find("invalidName") != std::string::npos);
    Command del;
    api::routeHttp("DELETE", std::string("/api/v1/apps/") + name, "", del, imm);
    TEST_ASSERT_TRUE_MESSAGE(del.type != CommandType::DeleteApp, name);
  }
  TEST_ASSERT_TRUE(api::isValidAppName("Next"));
}

static void test_http_script_put_routes_with_source() {
  Command c;
  api::HttpResult imm;
  const char* src = "def draw() text(0,6,'hi',0xFFFFFF) end";
  TEST_ASSERT_EQUAL_INT(ro(api::RouteOutcome::Routed),
                        ro(api::routeHttp("PUT", "/api/v1/apps/script/Demo", src, c, imm)));
  TEST_ASSERT_EQUAL_INT(ct(CommandType::ScriptSet), ct(c.type));
  TEST_ASSERT_EQUAL_STRING("Demo", c.name.c_str());
  TEST_ASSERT_EQUAL_STRING(src, c.payload.c_str());
  TEST_ASSERT_EQUAL_INT((int)Source::Http, (int)c.source);
}

static void test_http_guarded_update_routes_and_reports_conflicts() {
  Command c;
  api::HttpResult imm;
  const std::string body = "{\"expected_source\":null,\"source\":\"code\"}";
  TEST_ASSERT_EQUAL_INT(ro(api::RouteOutcome::Routed),
      ro(api::routeHttp("PUT", "/api/v1/apps/script-update/Demo", std::string(body), c, imm)));
  TEST_ASSERT_EQUAL_INT(ct(CommandType::ScriptUpdate), ct(c.type));
  TEST_ASSERT_EQUAL_STRING("Demo", c.name.c_str());
  TEST_ASSERT_EQUAL_STRING(body.c_str(), c.payload.c_str());
  TEST_ASSERT_TRUE(api::isRawBodyWrite("PUT", "/api/v1/apps/script-update/Demo"));
  const auto conflict = api::httpResponse(c, DispatchResult::Conflict, {});
  TEST_ASSERT_EQUAL_INT(409, conflict.status);
  TEST_ASSERT_TRUE(conflict.body.find("scriptChanged") != std::string::npos);
  TEST_ASSERT_EQUAL_INT(ro(api::RouteOutcome::Respond),
      ro(api::routeHttp("PUT", "/api/v1/apps/script-update/../x", "{}", c, imm)));
  TEST_ASSERT_EQUAL_INT(400, imm.status);
  TEST_ASSERT_EQUAL_INT(ro(api::RouteOutcome::Respond),
      ro(api::routeHttp("POST", "/api/v1/apps/script-update/Demo", "{}", c, imm)));
  TEST_ASSERT_EQUAL_INT(405, imm.status);
}

static void test_http_script_traversal_name_rejected() {
  Command c;
  api::HttpResult imm;
  TEST_ASSERT_EQUAL_INT(ro(api::RouteOutcome::Respond),
                        ro(api::routeHttp("PUT", "/api/v1/apps/script/../x", "x", c, imm)));
  TEST_ASSERT_EQUAL_INT(400, imm.status);
  TEST_ASSERT_TRUE(imm.body.find("invalidName") != std::string::npos);
  TEST_ASSERT_EQUAL_INT(ro(api::RouteOutcome::Respond),
                        ro(api::routeHttp("PUT", "/api/v1/apps/script/a.ax", "x", c, imm)));
  TEST_ASSERT_EQUAL_INT(400, imm.status);
}

static void test_script_write_is_exempt_from_the_json_gate() {
  TEST_ASSERT_TRUE(api::isRawBodyWrite("PUT", "/api/v1/apps/script/Clock"));

  TEST_ASSERT_TRUE(api::isRawBodyWrite("PUT", "/api/v1/apps/script/a.ax"));
  Command c;
  api::HttpResult imm;
  TEST_ASSERT_EQUAL_INT(ro(api::RouteOutcome::Respond),
                        ro(api::routeHttp("PUT", "/api/v1/apps/script/a.ax", "x", c, imm)));
  TEST_ASSERT_EQUAL_INT(400, imm.status);
  TEST_ASSERT_TRUE(imm.body.find("invalidName") != std::string::npos);

  TEST_ASSERT_FALSE(api::isRawBodyWrite("PUT", "/api/v1/apps/script/"));
  TEST_ASSERT_FALSE(api::isRawBodyWrite("PUT", "/api/v1/apps/pushed/Clock"));
  TEST_ASSERT_FALSE(api::isRawBodyWrite("PUT", "/api/v1/settings"));
  TEST_ASSERT_FALSE(api::isRawBodyWrite("GET", "/api/v1/apps/script/Clock"));
  TEST_ASSERT_FALSE(api::isRawBodyWrite("PATCH", "/api/v1/apps/script/Clock"));
}

static void test_http_delete_app_is_kind_agnostic() {
  Command c;
  api::HttpResult imm;
  TEST_ASSERT_EQUAL_INT(ro(api::RouteOutcome::Routed),
                        ro(api::routeHttp("DELETE", "/api/v1/apps/Demo", "", c, imm)));
  TEST_ASSERT_EQUAL_INT(ct(CommandType::DeleteApp), ct(c.type));
  TEST_ASSERT_EQUAL_STRING("Demo", c.name.c_str());
  TEST_ASSERT_TRUE(c.clear);
  Command c2;
  TEST_ASSERT_EQUAL_INT(ro(api::RouteOutcome::Routed),
                        ro(api::routeHttp("DELETE", "/api/v1/apps/Ghost", "", c2, imm)));
  TEST_ASSERT_EQUAL_INT(ct(CommandType::DeleteApp), ct(c2.type));
  TEST_ASSERT_EQUAL_INT(ro(api::RouteOutcome::Respond),
                        ro(api::routeHttp("DELETE", "/api/v1/apps/a/b", "", c, imm)));
  TEST_ASSERT_EQUAL_INT(400, imm.status);
  TEST_ASSERT_EQUAL_INT(ro(api::RouteOutcome::Respond),
                        ro(api::routeHttp("GET", "/api/v1/apps/script/", "", c, imm)));
  TEST_ASSERT_EQUAL_INT(400, imm.status);
  TEST_ASSERT_EQUAL_INT(ro(api::RouteOutcome::Respond),
                        ro(api::routeHttp("PUT", "/api/v1/apps/pushed/", "{}", c, imm)));
  TEST_ASSERT_EQUAL_INT(400, imm.status);
  TEST_ASSERT_EQUAL_INT(ro(api::RouteOutcome::Respond),
                        ro(api::routeHttp("GET", "/api/v1/apps/Time", "", c, imm)));
  TEST_ASSERT_EQUAL_INT(405, imm.status);
}

static void test_http_reserved_app_paths_are_not_names() {
  Command c;
  api::HttpResult imm;
  for (const char* p : {"/api/v1/apps/active", "/api/v1/apps/next", "/api/v1/apps/previous",
                        "/api/v1/apps/order"}) {
    TEST_ASSERT_EQUAL_INT_MESSAGE(ro(api::RouteOutcome::Respond),
                                  ro(api::routeHttp("DELETE", p, "", c, imm)), p);
    TEST_ASSERT_EQUAL_INT_MESSAGE(405, imm.status, p);
  }
}

static void test_http_script_empty_source_rejected() {
  Command c;
  api::HttpResult imm;
  TEST_ASSERT_EQUAL_INT(ro(api::RouteOutcome::Respond),
                        ro(api::routeHttp("PUT", "/api/v1/apps/script/Demo", "", c, imm)));
  TEST_ASSERT_EQUAL_INT(422, imm.status);
}

static void test_http_script_get_is_read_and_others_405() {
  Command c;
  api::HttpResult imm;
  TEST_ASSERT_EQUAL_INT(ro(api::RouteOutcome::NoMatch),
                        ro(api::routeHttp("GET", "/api/v1/apps/script/Demo", "", c, imm)));
  TEST_ASSERT_EQUAL_INT(ro(api::RouteOutcome::Respond),
                        ro(api::routeHttp("POST", "/api/v1/apps/script/Demo", "x", c, imm)));
  TEST_ASSERT_EQUAL_INT(405, imm.status);
  TEST_ASSERT_EQUAL_INT(ro(api::RouteOutcome::NoMatch),
                        ro(api::routeHttp("GET", "/api/v1/apps", "", c, imm)));
}

static void test_http_script_config_routes() {
  Command c = routed("PATCH", "/api/v1/apps/Weather/config", "{\"city\":\"Rom\"}");
  TEST_ASSERT_EQUAL_INT(ct(CommandType::ScriptConfigSet), ct(c.type));
  TEST_ASSERT_EQUAL_STRING("Weather", c.name.c_str());
  TEST_ASSERT_EQUAL_STRING("{\"city\":\"Rom\"}", c.payload.c_str());

  api::HttpResult imm;
  TEST_ASSERT_EQUAL_INT(ro(api::RouteOutcome::NoMatch),
                        ro(api::routeHttp("GET", "/api/v1/apps/Weather/config", "", c, imm)));
  TEST_ASSERT_EQUAL_INT(ro(api::RouteOutcome::Respond),
                        ro(api::routeHttp("DELETE", "/api/v1/apps/Weather/config", "", c, imm)));
  TEST_ASSERT_EQUAL_INT(405, imm.status);
  TEST_ASSERT_EQUAL_INT(ro(api::RouteOutcome::Respond),
                        ro(api::routeHttp("PATCH", "/api/v1/apps/Weather/config", "", c, imm)));
  TEST_ASSERT_EQUAL_INT(422, imm.status);
  TEST_ASSERT_EQUAL_INT(
      ro(api::RouteOutcome::Respond),
      ro(api::routeHttp("PATCH", "/api/v1/apps/../etc/config", "{}", c, imm)));
  TEST_ASSERT_EQUAL_INT(400, imm.status);
}

static void test_http_script_config_is_claimed_before_the_catch_all() {
  Command c;
  api::HttpResult imm;
  TEST_ASSERT_EQUAL_INT(ro(api::RouteOutcome::Routed),
                        ro(api::routeHttp("DELETE", "/api/v1/apps/Weather", "", c, imm)));
  TEST_ASSERT_EQUAL_INT(ct(CommandType::DeleteApp), ct(c.type));

  TEST_ASSERT_EQUAL_INT(ro(api::RouteOutcome::NoMatch),
                        ro(api::routeHttp("GET", "/api/v1/apps/script/config", "", c, imm)));
  TEST_ASSERT_EQUAL_INT(ro(api::RouteOutcome::Routed),
                        ro(api::routeHttp("PUT", "/api/v1/apps/script/config", "x", c, imm)));
  TEST_ASSERT_EQUAL_INT(ct(CommandType::ScriptSet), ct(c.type));
  TEST_ASSERT_EQUAL_STRING("config", c.name.c_str());
}

static void test_http_response_script_config_set_reports_error_state() {
  Command set(CommandType::ScriptConfigSet);
  set.name = "Weather";
  DispatchDetail raised;
  raised.message = "runtime_error: operand must be number";
  raised.line = 4;
  raised.hook = "init";
  auto rt = api::httpResponse(set, DispatchResult::Ok, raised);
  TEST_ASSERT_EQUAL_INT(200, rt.status);
  TEST_ASSERT_TRUE(rt.body.find("\"name\":\"Weather\"") != std::string::npos);
  TEST_ASSERT_TRUE(rt.body.find("\"line\":4") != std::string::npos);
  TEST_ASSERT_TRUE(rt.body.find("\"hook\":\"init\"") != std::string::npos);
}

static void test_http_script_data_routes() {
  Command c = routed("PATCH", "/api/v1/apps/Game/data", "{\"unl\":9}");
  TEST_ASSERT_EQUAL_INT(ct(CommandType::ScriptDataSet), ct(c.type));
  TEST_ASSERT_EQUAL_STRING("Game", c.name.c_str());
  TEST_ASSERT_EQUAL_STRING("{\"unl\":9}", c.payload.c_str());

  api::HttpResult imm;
  TEST_ASSERT_EQUAL_INT(ro(api::RouteOutcome::NoMatch),
                        ro(api::routeHttp("GET", "/api/v1/apps/Game/data", "", c, imm)));
  TEST_ASSERT_EQUAL_INT(ro(api::RouteOutcome::Respond),
                        ro(api::routeHttp("PUT", "/api/v1/apps/Game/data", "{}", c, imm)));
  TEST_ASSERT_EQUAL_INT(405, imm.status);
  TEST_ASSERT_EQUAL_INT(ro(api::RouteOutcome::Respond),
                        ro(api::routeHttp("PATCH", "/api/v1/apps/Game/data", "", c, imm)));
  TEST_ASSERT_EQUAL_INT(422, imm.status);
  TEST_ASSERT_EQUAL_INT(ro(api::RouteOutcome::Respond),
                        ro(api::routeHttp("PATCH", "/api/v1/apps/../etc/data", "{}", c, imm)));
  TEST_ASSERT_EQUAL_INT(400, imm.status);

  TEST_ASSERT_EQUAL_INT(ro(api::RouteOutcome::Routed),
                        ro(api::routeHttp("DELETE", "/api/v1/apps/data", "", c, imm)));
  TEST_ASSERT_EQUAL_INT(ct(CommandType::DeleteApp), ct(c.type));
  TEST_ASSERT_EQUAL_STRING("data", c.name.c_str());
}

static void test_http_response_script_data_set_reports_error_state() {
  Command set(CommandType::ScriptDataSet);
  set.name = "Game";
  DispatchDetail raised;
  raised.message = "runtime_error: operand must be number";
  raised.line = 4;
  auto rt = api::httpResponse(set, DispatchResult::Ok, raised);
  TEST_ASSERT_EQUAL_INT(200, rt.status);
  TEST_ASSERT_TRUE(rt.body.find("\"name\":\"Game\"") != std::string::npos);
  TEST_ASSERT_TRUE(rt.body.find("\"line\":4") != std::string::npos);
}

static void test_http_response_script_set_reports_error_state() {
  Command set(CommandType::ScriptSet);
  set.name = "Demo";
  DispatchDetail none;
  auto ok = api::httpResponse(set, DispatchResult::Ok, none);
  TEST_ASSERT_EQUAL_INT(200, ok.status);
  TEST_ASSERT_TRUE(ok.body.find("\"error\":null") != std::string::npos);
  TEST_ASSERT_TRUE(ok.body.find("Demo") != std::string::npos);

  DispatchDetail broken;
  broken.message = "syntax_error: unexpected token 'end'";
  broken.line = 12;
  auto bad = api::httpResponse(set, DispatchResult::Ok, broken);
  TEST_ASSERT_EQUAL_INT(200, bad.status);
  TEST_ASSERT_TRUE(bad.body.find("syntax_error") != std::string::npos);
  TEST_ASSERT_TRUE(bad.body.find("\"line\":12") != std::string::npos);
  TEST_ASSERT_TRUE(bad.body.find("hook") == std::string::npos);

  DispatchDetail raised;
  raised.message = "runtime_error: operand must be number";
  raised.hook = "setup";
  auto rt = api::httpResponse(set, DispatchResult::Ok, raised);
  TEST_ASSERT_EQUAL_INT(200, rt.status);
  TEST_ASSERT_TRUE(rt.body.find("\"hook\":\"setup\"") != std::string::npos);
  TEST_ASSERT_TRUE(rt.body.find("line") == std::string::npos);

  DispatchDetail full{"", "script limit reached (max 6)"};
  auto cap = api::httpResponse(set, DispatchResult::Capacity, full);
  TEST_ASSERT_EQUAL_INT(507, cap.status);
}


static void test_mqtt_commands() {
  Command c;
  std::string res;
  TEST_ASSERT_EQUAL_INT(ro(api::RouteOutcome::Routed),
                        ro(routeMqttCopy("cmd/notify", "{\"text\":\"x\"}", c, res)));
  TEST_ASSERT_EQUAL_INT(ct(CommandType::Notify), ct(c.type));
  TEST_ASSERT_EQUAL_INT((int)Source::Mqtt, (int)c.source);

  TEST_ASSERT_EQUAL_INT(ro(api::RouteOutcome::Routed),
                        ro(routeMqttCopy("cmd/apps/pushed/clock", "{}", c, res)));
  TEST_ASSERT_EQUAL_INT(ct(CommandType::SetPushedApp), ct(c.type));
  TEST_ASSERT_EQUAL_STRING("clock", c.name.c_str());
  TEST_ASSERT_FALSE(c.clear);

  TEST_ASSERT_EQUAL_INT(ro(api::RouteOutcome::Routed),
                        ro(routeMqttCopy("cmd/apps/pushed/clock", "", c, res)));
  TEST_ASSERT_TRUE(c.clear);

  TEST_ASSERT_EQUAL_INT(ro(api::RouteOutcome::Routed),
                        ro(routeMqttCopy("cmd/screen/get", "", c, res)));
  TEST_ASSERT_EQUAL_INT(ct(CommandType::SendScreen), ct(c.type));

  TEST_ASSERT_EQUAL_INT(ro(api::RouteOutcome::Routed),
                        ro(routeMqttCopy("cmd/settings", "{\"brightness\":1}", c, res)));
  TEST_ASSERT_EQUAL_INT(ct(CommandType::SetSettings), ct(c.type));

  TEST_ASSERT_EQUAL_INT(ro(api::RouteOutcome::Routed),
                        ro(routeMqttCopy("cmd/indicators/1", "", c, res)));
  TEST_ASSERT_EQUAL_INT(ct(CommandType::SetIndicator), ct(c.type));
  TEST_ASSERT_EQUAL_INT(1, c.arg);
}

static void test_mqtt_pushed_app_name_is_validated() {
  Command c;
  std::string res;
  TEST_ASSERT_EQUAL_INT(
      ro(api::RouteOutcome::Respond),
      ro(routeMqttCopy("cmd/apps/pushed/../x", "{\"text\":\"x\"}", c, res)));
  TEST_ASSERT_TRUE(res.find("invalidName") != std::string::npos);
  TEST_ASSERT_TRUE(res.find("\"ok\":false") != std::string::npos);
  TEST_ASSERT_EQUAL_INT(ro(api::RouteOutcome::Respond),
                        ro(routeMqttCopy("cmd/apps/pushed/a/b", "{}", c, res)));
  TEST_ASSERT_TRUE(res.find("invalidName") != std::string::npos);
}

static void test_mqtt_result_echo_detection() {
  TEST_ASSERT_TRUE(api::isResultEcho("cmd/settings/result"));
  TEST_ASSERT_TRUE(api::isResultEcho("cmd/notify/result"));
  TEST_ASSERT_TRUE(api::isResultEcho("cmd/apps/pushed/weather/result"));
  TEST_ASSERT_FALSE(api::isResultEcho("cmd/apps/pushed/result"));
  TEST_ASSERT_FALSE(api::isResultEcho("cmd/notify"));
  TEST_ASSERT_FALSE(api::isResultEcho("state/device"));

  Command c;
  std::string res;
  TEST_ASSERT_EQUAL_INT(ro(api::RouteOutcome::Routed),
                        ro(routeMqttCopy("cmd/apps/pushed/result", "{\"text\":\"x\"}", c, res)));
  TEST_ASSERT_EQUAL_STRING("result", c.name.c_str());
}

static void test_mqtt_no_factory_reset_and_unknown() {
  Command c;
  std::string res;
  TEST_ASSERT_EQUAL_INT(ro(api::RouteOutcome::NoMatch),
                        ro(routeMqttCopy("cmd/device/factory-reset", "", c, res)));
  TEST_ASSERT_EQUAL_INT(ro(api::RouteOutcome::NoMatch),
                        ro(routeMqttCopy("brightness", "120", c, res)));
  TEST_ASSERT_EQUAL_INT(ro(api::RouteOutcome::NoMatch),
                        ro(routeMqttCopy("notify", "{}", c, res)));
}

static void test_mqtt_bad_body_responds_error() {
  Command c;
  std::string res;
  TEST_ASSERT_EQUAL_INT(ro(api::RouteOutcome::Respond),
                        ro(routeMqttCopy("cmd/audio/play", "{bad", c, res)));
  TEST_ASSERT_TRUE(res.find("invalidJson") != std::string::npos);
}


static void test_http_response_ok_and_errors() {
  Command notify(CommandType::Notify);
  DispatchDetail none;

  auto ok = api::httpResponse(notify, DispatchResult::Ok, none);
  TEST_ASSERT_EQUAL_INT(200, ok.status);
  TEST_ASSERT_EQUAL_STRING("application/json", ok.contentType);

  auto bad = api::httpResponse(notify, DispatchResult::ParseError, none);
  TEST_ASSERT_EQUAL_INT(400, bad.status);
  TEST_ASSERT_TRUE(bad.body.find("invalidJson") != std::string::npos);

  DispatchDetail d{"brightness", "out of range"};
  Command set(CommandType::SetSettings);
  auto invalid = api::httpResponse(set, DispatchResult::ValidationError, d);
  TEST_ASSERT_EQUAL_INT(422, invalid.status);
  TEST_ASSERT_TRUE(invalid.body.find("validationFailed") != std::string::npos);
  TEST_ASSERT_TRUE(invalid.body.find("brightness") != std::string::npos);

  Command sw(CommandType::SwitchApp);
  auto nf = api::httpResponse(sw, DispatchResult::NotFound, none);
  TEST_ASSERT_EQUAL_INT(404, nf.status);
  TEST_ASSERT_TRUE(nf.body.find("notFound") != std::string::npos);

  auto fail = api::httpResponse(notify, DispatchResult::Failed, none);
  TEST_ASSERT_EQUAL_INT(500, fail.status);
}

static void test_http_response_capacity_is_507() {
  Command app(CommandType::SetPushedApp);
  DispatchDetail d{"", "pushed app store is full (max 20)"};
  auto cap = api::httpResponse(app, DispatchResult::Capacity, d);
  TEST_ASSERT_EQUAL_INT(507, cap.status);
  TEST_ASSERT_TRUE(cap.body.find("insufficientStorage") != std::string::npos);
  TEST_ASSERT_TRUE(cap.body.find("full") != std::string::npos);
}

static void test_http_response_busy_is_503_with_retry_after() {
  Command set(CommandType::ScriptSet);
  DispatchDetail d{"name", "a script fetch is in flight, try again"};
  auto busy = api::httpResponse(set, DispatchResult::Busy, d);
  TEST_ASSERT_EQUAL_INT(503, busy.status);
  TEST_ASSERT_EQUAL_INT(2, busy.retryAfterSeconds);
  TEST_ASSERT_TRUE(busy.body.find("serviceBusy") != std::string::npos);
  TEST_ASSERT_TRUE(busy.body.find("try again") != std::string::npos);
}

static void test_error_envelope_is_valid_json() {
  const std::string e = api::errorJson("validationFailed", "out of range", "alertVolume");
  api::JsonReader probe{std::string_view(e)};
  TEST_ASSERT_TRUE(probe.skipValue() && probe.atEnd());
  const api::JsonReader err = api::memberValue(api::JsonReader(e), "error");
  auto field = [&](const char* k) {
    std::string v;
    api::memberValue(err, k).appendString(v);
    return v;
  };
  TEST_ASSERT_EQUAL_STRING("validationFailed", field("code").c_str());
  TEST_ASSERT_EQUAL_STRING("out of range", field("message").c_str());
  TEST_ASSERT_EQUAL_STRING("alertVolume", field("field").c_str());
}

static void test_mqtt_result_payloads() {
  DispatchDetail none;
  TEST_ASSERT_EQUAL_STRING("{\"ok\":true}", api::mqttResult(DispatchResult::Ok, none).c_str());
  DispatchDetail d{"alertVolume", "out of range"};
  const std::string r = api::mqttResult(DispatchResult::ValidationError, d);
  TEST_ASSERT_TRUE(r.find("\"ok\":false") != std::string::npos);
  TEST_ASSERT_TRUE(r.find("validationFailed") != std::string::npos);
  TEST_ASSERT_TRUE(r.find("alertVolume") != std::string::npos);
}

static void test_error_event_wraps_http_and_mqtt_errors() {
  const std::string err = "\"error\":{\"code\":\"validationFailed\",\"message\":\"m\",\"field\":\"text\"}}";
  TEST_ASSERT_EQUAL_STRING(
      ("{\"source\":\"http\",\"request\":\"PUT /api/v1/apps/pushed/a\"," + err).c_str(),
      api::errorEvent("http", "PUT /api/v1/apps/pushed/a", "{" + err).c_str());
  TEST_ASSERT_EQUAL_STRING(
      ("{\"source\":\"mqtt\",\"request\":\"p/cmd/notify\"," + err).c_str(),
      api::errorEvent("mqtt", "p/cmd/notify", "{\"ok\":false," + err).c_str());
  TEST_ASSERT_EQUAL_STRING("{\"source\":\"http\",\"request\":\"a\\\"b\",\"error\":{}}",
                           api::errorEvent("http", "a\"b", "{\"error\":{}}").c_str());
  TEST_ASSERT_TRUE(api::errorEvent("mqtt", "t", "{\"ok\":true}").empty());
  TEST_ASSERT_TRUE(api::errorEvent("http", "t", "{\"ok\":true,\"name\":\"x\"}").empty());
  TEST_ASSERT_TRUE(api::errorEvent("http", "t", "").empty());
  TEST_ASSERT_TRUE(api::errorEvent("http", "t", "{").empty());
}

static void test_delete_named_notification_routes_with_the_name() {
  Command c; api::HttpResult r;
  TEST_ASSERT_EQUAL_INT((int)api::RouteOutcome::Routed,
                        (int)api::routeHttp("DELETE", "/api/v1/notifications/backup-job", "", c, r));
  TEST_ASSERT_EQUAL_INT((int)CommandType::DismissNotify, (int)c.type);
  TEST_ASSERT_EQUAL_STRING("backup-job", c.name.c_str());
}

static void test_delete_active_notification_carries_no_name() {
  Command c; api::HttpResult r;
  TEST_ASSERT_EQUAL_INT((int)api::RouteOutcome::Routed,
                        (int)api::routeHttp("DELETE", "/api/v1/notifications/active", "", c, r));
  TEST_ASSERT_EQUAL_INT((int)CommandType::DismissNotify, (int)c.type);
  TEST_ASSERT_EQUAL_STRING("", c.name.c_str());
}

static void test_named_notification_rejects_other_methods() {
  Command c; api::HttpResult r;
  TEST_ASSERT_EQUAL_INT((int)api::RouteOutcome::Respond,
                        (int)api::routeHttp("POST", "/api/v1/notifications/backup-job", "", c, r));
  TEST_ASSERT_EQUAL_INT(405, r.status);
}

static void test_mqtt_dismiss_by_name() {
  Command c; std::string res;
  TEST_ASSERT_EQUAL_INT((int)api::RouteOutcome::Routed,
                        (int)routeMqttCopy("cmd/notify/dismiss/backup-job", "", c, res));
  TEST_ASSERT_EQUAL_INT((int)CommandType::DismissNotify, (int)c.type);
  TEST_ASSERT_EQUAL_STRING("backup-job", c.name.c_str());
  Command c2; std::string res2;
  routeMqttCopy("cmd/notify/dismiss", "", c2, res2);
  TEST_ASSERT_EQUAL_STRING("", c2.name.c_str());
}

static void test_method_override_absent_leaves_the_method_alone() {
  api::MethodResolution r = api::resolveHttpMethod("POST", "/api/v1/display", "");
  TEST_ASSERT_NULL(r.error);
  TEST_ASSERT_EQUAL_STRING("POST", r.method.c_str());

  r = api::resolveHttpMethod("PATCH", "/api/v1/display", "   ");
  TEST_ASSERT_NULL(r.error);
  TEST_ASSERT_EQUAL_STRING("PATCH", r.method.c_str());
}

static void test_method_override_maps_post_onto_the_write_verbs() {
  api::MethodResolution r = api::resolveHttpMethod("POST", "/api/v1/display", "PATCH");
  TEST_ASSERT_NULL(r.error);
  TEST_ASSERT_EQUAL_STRING("PATCH", r.method.c_str());

  r = api::resolveHttpMethod("POST", "/api/v1/apps/pushed/test", " put ");
  TEST_ASSERT_NULL(r.error);
  TEST_ASSERT_EQUAL_STRING("PUT", r.method.c_str());

  r = api::resolveHttpMethod("POST", "/api/v1/apps/test", "delete");
  TEST_ASSERT_NULL(r.error);
  TEST_ASSERT_EQUAL_STRING("DELETE", r.method.c_str());
}

static void test_method_override_is_post_only_and_verb_limited() {
  api::MethodResolution r = api::resolveHttpMethod("GET", "/api/v1/display", "PATCH");
  TEST_ASSERT_NOT_NULL(r.error);
  TEST_ASSERT_EQUAL_STRING("GET", r.method.c_str());

  r = api::resolveHttpMethod("PUT", "/api/v1/display", "PATCH");
  TEST_ASSERT_NOT_NULL(r.error);

  r = api::resolveHttpMethod("POST", "/api/v1/display", "GET");
  TEST_ASSERT_NOT_NULL(r.error);

  r = api::resolveHttpMethod("POST", "/api/v1/display", "POST");
  TEST_ASSERT_NOT_NULL(r.error);

  r = api::resolveHttpMethod("POST", "/api/v1/display", "TRACE");
  TEST_ASSERT_NOT_NULL(r.error);
}

static void test_method_override_cannot_reach_the_raw_script_upload() {
  const api::MethodResolution r =
      api::resolveHttpMethod("POST", "/api/v1/apps/script/test", "PUT");
  TEST_ASSERT_NOT_NULL(r.error);
  TEST_ASSERT_EQUAL_STRING("POST", r.method.c_str());
}

static void test_method_override_routes_like_the_real_verb() {
  const api::MethodResolution r = api::resolveHttpMethod("POST", "/api/v1/display", "patch");
  Command c;
  api::HttpResult imm;
  TEST_ASSERT_EQUAL_INT(
      ro(api::RouteOutcome::Routed),
      ro(api::routeHttp(r.method, "/api/v1/display", "{\"power\":false}", c, imm)));
  TEST_ASSERT_EQUAL_INT(ct(CommandType::SetDisplay), ct(c.type));
  TEST_ASSERT_EQUAL_STRING("{\"power\":false}", c.payload.c_str());
}

static void test_http_owned_payloads_transfer_without_copying() {
  for (const char* path : {"/api/v1/apps/pushed/ramtest", "/api/v1/indicators/1"}) {
    std::string body = "{\"text\":\"" + std::string(4096, 'x') + "\"}";
    const char* bytes = body.data();
    const std::string expected = body;
    Command c;
    api::HttpResult imm;
    TEST_ASSERT_EQUAL_INT(ro(api::RouteOutcome::Routed),
                         ro(api::routeHttp("PUT", path, std::move(body), c, imm)));
    TEST_ASSERT_EQUAL_STRING(expected.c_str(), c.payload.c_str());
    TEST_ASSERT_EQUAL_PTR(bytes, c.payload.data());
  }
}

static void test_mqtt_routed_payloads_transfer_without_copying() {
  for (const char* suffix : {"cmd/notify", "cmd/settings", "cmd/apps/pushed/ramtest",
                             "cmd/display/moodlight", "cmd/indicators/1", "cmd/audio/play"}) {
    std::string body = "{\"song\":\"" + std::string(4096, 'x') + "\"}";
    const char* bytes = body.data();
    const std::string expected = body;
    Command c;
    std::string result;
    TEST_ASSERT_EQUAL_INT(ro(api::RouteOutcome::Routed),
                         ro(api::routeMqtt(suffix, body, c, result)));
    TEST_ASSERT_EQUAL_STRING(expected.c_str(), c.payload.c_str());
    TEST_ASSERT_EQUAL_PTR(bytes, c.payload.data());
    TEST_ASSERT_FALSE(c.clear);
    TEST_ASSERT_EQUAL_INT(static_cast<int>(Source::Mqtt), static_cast<int>(c.source));
  }
}

static void test_mqtt_unrouted_payload_remains_available_to_platform() {
  for (const char* suffix : {"cmd/platform-specific", "cmd/indicators/4", "cmd/apps/pushed/a%2Fb",
                             "cmd/audio/play"}) {
    std::string body(4096, 'x');
    const char* bytes = body.data();
    Command c;
    std::string result;
    TEST_ASSERT_NOT_EQUAL(ro(api::RouteOutcome::Routed),
                          ro(api::routeMqtt(suffix, body, c, result)));
    TEST_ASSERT_EQUAL_size_t(4096, body.size());
    TEST_ASSERT_EQUAL_PTR(bytes, body.data());
  }
}


void test_command_response_returns_settings_and_persistence_failure() {
  Clock clock;
  Command command(CommandType::SetSettings);
  command.payload = "{\"brightness\":42}";
  auto result = clock.engine.execute(command);
  auto response = api::commandResponse(clock.engine, command, result);
  TEST_ASSERT_EQUAL_INT(200, response.status);
  long long brightness = 0;
  TEST_ASSERT_TRUE(api::memberValue(api::JsonReader(response.body), "brightness").asLong(brightness));
  TEST_ASSERT_EQUAL_INT(42, brightness);
  command.type = CommandType::SetAppOrder;
  response = api::commandResponse(clock.engine, command, DispatchResult::Ok, true);
  TEST_ASSERT_EQUAL_INT(507, response.status);
  response = api::commandResponse(clock.engine, command, DispatchResult::ParseError, true);
  TEST_ASSERT_EQUAL_INT(400, response.status);
}

void test_origin_requires_one_exact_authority() {
  TEST_ASSERT_TRUE(api::sameOrigin("", "clock.local", 0, 1));
  TEST_ASSERT_FALSE(api::sameOrigin("", "clock.local", 0, 1, true));
  TEST_ASSERT_TRUE(api::sameOrigin("http://clock.local:8080", "clock.local:8080", 1, 1, true));
  TEST_ASSERT_TRUE(api::sameOrigin("https://clock.local", "clock.local", 1, 1, true));
  TEST_ASSERT_FALSE(api::sameOrigin("http://clock.local.evil", "clock.local", 1, 1));
  TEST_ASSERT_FALSE(api::sameOrigin("http://clock.local/", "clock.local", 1, 1));
  TEST_ASSERT_FALSE(api::sameOrigin("null", "clock.local", 1, 1));
  TEST_ASSERT_FALSE(api::sameOrigin("http://clock.local", "clock.local", 2, 1));
  TEST_ASSERT_FALSE(api::sameOrigin("http://clock.local", "clock.local", 1, 2));
  TEST_ASSERT_FALSE(api::sameOrigin("http://", "", 1, 1));
}

int main(int, char**) {
  UNITY_BEGIN();
  RUN_TEST(test_origin_requires_one_exact_authority);
  RUN_TEST(test_command_response_returns_settings_and_persistence_failure);
  RUN_TEST(test_http_owned_payloads_transfer_without_copying);
  RUN_TEST(test_mqtt_routed_payloads_transfer_without_copying);
  RUN_TEST(test_mqtt_unrouted_payload_remains_available_to_platform);
  RUN_TEST(test_method_override_absent_leaves_the_method_alone);
  RUN_TEST(test_method_override_maps_post_onto_the_write_verbs);
  RUN_TEST(test_method_override_is_post_only_and_verb_limited);
  RUN_TEST(test_method_override_cannot_reach_the_raw_script_upload);
  RUN_TEST(test_method_override_routes_like_the_real_verb);
  RUN_TEST(test_delete_named_notification_routes_with_the_name);
  RUN_TEST(test_delete_active_notification_carries_no_name);
  RUN_TEST(test_named_notification_rejects_other_methods);
  RUN_TEST(test_mqtt_dismiss_by_name);
  RUN_TEST(test_http_radio_routes);
  RUN_TEST(test_http_radio_read_falls_through_to_the_transport);
  RUN_TEST(test_http_radio_wrong_methods_are_405_not_404);
  RUN_TEST(test_http_radio_play_needs_a_body);
  RUN_TEST(test_mqtt_radio_ops);
  RUN_TEST(test_http_notifications);
  RUN_TEST(test_http_pushed_apps_name_from_path);
  RUN_TEST(test_http_pushed_app_name_is_validated);
  RUN_TEST(test_http_settings_methods);
  RUN_TEST(test_http_display);
  RUN_TEST(test_http_apps);
  RUN_TEST(test_http_app_switch_names_the_app_in_the_path);
  RUN_TEST(test_mqtt_app_switch_names_the_app_in_the_topic);
  RUN_TEST(test_app_switch_answers_over_http_and_mqtt);
  RUN_TEST(test_http_indicators);
  RUN_TEST(test_http_audio_clip);
  RUN_TEST(test_http_play_routes_the_sound_object);
  RUN_TEST(test_http_play_rejects_with_the_parsers_field);
  RUN_TEST(test_http_play_rejects_malformed_json_with_400);
  RUN_TEST(test_http_stop_takes_a_group);
  RUN_TEST(test_mqtt_audio_shares_the_http_rules);
  RUN_TEST(test_http_sounds_play_no_longer_knows_builtin);
  RUN_TEST(test_http_sounds_stop);
  RUN_TEST(test_mqtt_sounds_share_the_http_validation);
  RUN_TEST(test_http_device_actions);
  RUN_TEST(test_http_reads_and_unknown_no_match);
  RUN_TEST(test_http_get_only_reads_reject_other_methods);
  RUN_TEST(test_http_shared_state_is_read_only);
  RUN_TEST(test_app_name_validation);
  RUN_TEST(test_names_of_fixed_app_routes_are_refused);
  RUN_TEST(test_http_script_put_routes_with_source);
  RUN_TEST(test_http_guarded_update_routes_and_reports_conflicts);
  RUN_TEST(test_http_script_traversal_name_rejected);
  RUN_TEST(test_http_delete_app_is_kind_agnostic);
  RUN_TEST(test_http_reserved_app_paths_are_not_names);
  RUN_TEST(test_http_script_empty_source_rejected);
  RUN_TEST(test_http_script_get_is_read_and_others_405);
  RUN_TEST(test_http_script_config_routes);
  RUN_TEST(test_http_script_config_is_claimed_before_the_catch_all);
  RUN_TEST(test_http_response_script_config_set_reports_error_state);
  RUN_TEST(test_http_script_data_routes);
  RUN_TEST(test_http_response_script_data_set_reports_error_state);
  RUN_TEST(test_http_response_script_set_reports_error_state);
  RUN_TEST(test_mqtt_commands);
  RUN_TEST(test_mqtt_pushed_app_name_is_validated);
  RUN_TEST(test_mqtt_result_echo_detection);
  RUN_TEST(test_mqtt_no_factory_reset_and_unknown);
  RUN_TEST(test_mqtt_bad_body_responds_error);
  RUN_TEST(test_http_response_ok_and_errors);
  RUN_TEST(test_http_response_capacity_is_507);
  RUN_TEST(test_http_response_busy_is_503_with_retry_after);
  RUN_TEST(test_error_envelope_is_valid_json);
  RUN_TEST(test_mqtt_result_payloads);
  RUN_TEST(test_error_event_wraps_http_and_mqtt_errors);
  RUN_TEST(test_script_write_is_exempt_from_the_json_gate);
  return UNITY_END();
}
