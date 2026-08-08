#include <unity.h>

#include <string>
#include <string_view>

#include "core/api/ApiRouter.h"
#include "core/api/JsonReader.h"
#include "core/script/ScriptServices.h"

using namespace awtrix;

void setUp() {}
void tearDown() {}

static int ct(CommandType t) { return static_cast<int>(t); }
static int ro(api::RouteOutcome o) { return static_cast<int>(o); }

static Command routed(const char* method, const char* path, const char* body = "") {
  Command c;
  api::HttpResult imm;
  TEST_ASSERT_EQUAL_INT_MESSAGE(ro(api::RouteOutcome::Routed),
                                ro(api::routeHttp(method, path, body, c, imm)), path);
  return c;
}


static void test_http_radio_routes() {
  // A stream keeps its payload whole: the dispatch reads station, index or url from it.
  Command c = routed("POST", "/api/v1/audio/play", "{\"station\":\"SWR3\"}");
  TEST_ASSERT_EQUAL_INT(ct(CommandType::RadioPlay), ct(c.type));
  TEST_ASSERT_EQUAL_STRING("{\"station\":\"SWR3\"}", c.payload.c_str());

  c = routed("POST", "/api/v1/audio/play", "{\"url\":\"http://s/x\"}");
  TEST_ASSERT_EQUAL_INT(ct(CommandType::RadioPlay), ct(c.type));

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
  Command c;
  api::HttpResult imm;
  TEST_ASSERT_EQUAL_INT(ro(api::RouteOutcome::Respond),
                        ro(api::routeHttp("POST", "/api/v1/audio/play", "{}", c, imm)));
  TEST_ASSERT_EQUAL_INT(422, imm.status);
}

static void test_mqtt_radio_ops() {
  Command c;
  std::string result;
  TEST_ASSERT_EQUAL_INT(ro(api::RouteOutcome::Routed),
                        ro(api::routeMqtt("cmd/audio/play", "{\"index\":0}", c, result)));
  TEST_ASSERT_EQUAL_INT(ct(CommandType::RadioPlay), ct(c.type));
  TEST_ASSERT_EQUAL_INT((int)Source::Mqtt, (int)c.source);

  TEST_ASSERT_EQUAL_INT(ro(api::RouteOutcome::Routed),
                        ro(api::routeMqtt("cmd/audio/stop", "", c, result)));
  TEST_ASSERT_EQUAL_INT(ct(CommandType::StopAudio), ct(c.type));

  TEST_ASSERT_EQUAL_INT(ro(api::RouteOutcome::Routed),
                        ro(api::routeMqtt("cmd/audio/stations", "[]", c, result)));
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
  TEST_ASSERT_TRUE(imm.body.find("DELETE /api/v1/apps/{name}") != std::string::npos);
  TEST_ASSERT_TRUE(imm.body.find("/api/v1/apps/pushed/{name}") == std::string::npos);
  TEST_ASSERT_EQUAL_INT(ro(api::RouteOutcome::Respond),
                        ro(api::routeHttp("PUT", "/api/v1/apps/pushed/weather", "", r, imm)));
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
}

static void test_http_sounds_play_variants() {
  Command c = routed("POST", "/api/v1/audio/play", "{\"clip\":\"alarm\"}");
  TEST_ASSERT_EQUAL_INT(ct(CommandType::PlayClip), ct(c.type));
  TEST_ASSERT_EQUAL_STRING("alarm", c.payload.c_str());

  c = routed("POST", "/api/v1/audio/play", "{\"melody\":\"alarm\"}");
  TEST_ASSERT_EQUAL_INT(ct(CommandType::PlayMelody), ct(c.type));
  TEST_ASSERT_EQUAL_STRING("alarm", c.payload.c_str());

  c = routed("POST", "/api/v1/audio/play", "{\"rtttl\":\"x:d=4:c\"}");
  TEST_ASSERT_EQUAL_INT(ct(CommandType::PlayRtttl), ct(c.type));
  TEST_ASSERT_EQUAL_STRING("x:d=4:c", c.payload.c_str());

  c = routed("POST", "/api/v1/audio/play", "{\"builtin\":\"r2d2\"}");
  TEST_ASSERT_EQUAL_INT(ct(CommandType::R2D2), ct(c.type));

  Command r;
  api::HttpResult imm;
  TEST_ASSERT_EQUAL_INT(ro(api::RouteOutcome::Respond),
                        ro(api::routeHttp("POST", "/api/v1/audio/play", "{bad", r, imm)));
  TEST_ASSERT_EQUAL_INT(400, imm.status);

  TEST_ASSERT_EQUAL_INT(ro(api::RouteOutcome::Respond),
                        ro(api::routeHttp("POST", "/api/v1/audio/play", "{}", r, imm)));
  TEST_ASSERT_EQUAL_INT(422, imm.status);

  TEST_ASSERT_EQUAL_INT(
      ro(api::RouteOutcome::Respond),
      ro(api::routeHttp("POST", "/api/v1/audio/play", "{\"clip\":\"a\",\"rtttl\":\"x:d=4:c\"}", r,
                        imm)));
  TEST_ASSERT_EQUAL_INT(422, imm.status);
}

static void test_http_sounds_play_rejects_unparseable_rtttl() {
  Command r;
  api::HttpResult imm;
  TEST_ASSERT_EQUAL_INT(
      ro(api::RouteOutcome::Respond),
      ro(api::routeHttp("POST", "/api/v1/audio/play",
                        "{\"rtttl\":\"d=4,o=5,b=120:c,e,g\"}", r, imm)));
  TEST_ASSERT_EQUAL_INT(422, imm.status);
  TEST_ASSERT_TRUE(imm.body.find("rtttl") != std::string::npos);

  TEST_ASSERT_EQUAL_INT(
      ro(api::RouteOutcome::Respond),
      ro(api::routeHttp("POST", "/api/v1/audio/play",
                        "{\"rtttl\":\"x:d=4,o=5,b=120:c,e,h\"}", r, imm)));
  TEST_ASSERT_EQUAL_INT(422, imm.status);
  TEST_ASSERT_TRUE(imm.body.find("not a note") != std::string::npos);
  TEST_ASSERT_TRUE(imm.body.find("offset") != std::string::npos);
}

static void test_http_sounds_stop() {
  Command c = routed("POST", "/api/v1/audio/stop");
  TEST_ASSERT_EQUAL_INT(ct(CommandType::StopAudio), ct(c.type));

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
                        ro(api::routeMqtt("cmd/audio/stop", "", c, res)));
  TEST_ASSERT_EQUAL_INT(ct(CommandType::StopAudio), ct(c.type));

  TEST_ASSERT_EQUAL_INT(
      ro(api::RouteOutcome::Respond),
      ro(api::routeMqtt("cmd/audio/play", "{\"rtttl\":\"x:d=4,o=5,b=120:zz\"}", c, res)));
  TEST_ASSERT_TRUE(res.find("\"ok\":false") != std::string::npos);
  TEST_ASSERT_TRUE(res.find("validationFailed") != std::string::npos);
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

static void test_http_script_oversize_source_rejected() {
  Command c;
  api::HttpResult imm;
  TEST_ASSERT_EQUAL_INT(
      ro(api::RouteOutcome::Respond),
      ro(api::routeHttp("PUT", "/api/v1/apps/script/Big",
                        std::string(script::maxSourceBytes() + 1, 'x'), c, imm)));
  TEST_ASSERT_EQUAL_INT(413, imm.status);
  TEST_ASSERT_TRUE(imm.body.find("payloadTooLarge") != std::string::npos);

  TEST_ASSERT_EQUAL_INT(
      ro(api::RouteOutcome::Routed),
      ro(api::routeHttp("PUT", "/api/v1/apps/script/Big",
                        std::string(script::maxSourceBytes(), 'x'), c, imm)));
  TEST_ASSERT_EQUAL_INT(script::maxSourceBytes(), c.payload.size());
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
                        ro(api::routeMqtt("cmd/notify", "{\"text\":\"x\"}", c, res)));
  TEST_ASSERT_EQUAL_INT(ct(CommandType::Notify), ct(c.type));
  TEST_ASSERT_EQUAL_INT((int)Source::Mqtt, (int)c.source);

  TEST_ASSERT_EQUAL_INT(ro(api::RouteOutcome::Routed),
                        ro(api::routeMqtt("cmd/apps/pushed/clock", "{}", c, res)));
  TEST_ASSERT_EQUAL_INT(ct(CommandType::SetPushedApp), ct(c.type));
  TEST_ASSERT_EQUAL_STRING("clock", c.name.c_str());
  TEST_ASSERT_FALSE(c.clear);

  TEST_ASSERT_EQUAL_INT(ro(api::RouteOutcome::Routed),
                        ro(api::routeMqtt("cmd/apps/pushed/clock", "", c, res)));
  TEST_ASSERT_TRUE(c.clear);

  TEST_ASSERT_EQUAL_INT(ro(api::RouteOutcome::Routed),
                        ro(api::routeMqtt("cmd/screen/get", "", c, res)));
  TEST_ASSERT_EQUAL_INT(ct(CommandType::SendScreen), ct(c.type));

  TEST_ASSERT_EQUAL_INT(ro(api::RouteOutcome::Routed),
                        ro(api::routeMqtt("cmd/settings", "{\"brightness\":1}", c, res)));
  TEST_ASSERT_EQUAL_INT(ct(CommandType::SetSettings), ct(c.type));

  TEST_ASSERT_EQUAL_INT(ro(api::RouteOutcome::Routed),
                        ro(api::routeMqtt("cmd/indicators/1", "", c, res)));
  TEST_ASSERT_EQUAL_INT(ct(CommandType::SetIndicator), ct(c.type));
  TEST_ASSERT_EQUAL_INT(1, c.arg);
}

static void test_mqtt_pushed_app_name_is_validated() {
  Command c;
  std::string res;
  TEST_ASSERT_EQUAL_INT(
      ro(api::RouteOutcome::Respond),
      ro(api::routeMqtt("cmd/apps/pushed/../x", "{\"text\":\"x\"}", c, res)));
  TEST_ASSERT_TRUE(res.find("invalidName") != std::string::npos);
  TEST_ASSERT_TRUE(res.find("\"ok\":false") != std::string::npos);
  TEST_ASSERT_EQUAL_INT(ro(api::RouteOutcome::Respond),
                        ro(api::routeMqtt("cmd/apps/pushed/a/b", "{}", c, res)));
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
                        ro(api::routeMqtt("cmd/apps/pushed/result", "{\"text\":\"x\"}", c, res)));
  TEST_ASSERT_EQUAL_STRING("result", c.name.c_str());
}

static void test_mqtt_no_factory_reset_and_unknown() {
  Command c;
  std::string res;
  TEST_ASSERT_EQUAL_INT(ro(api::RouteOutcome::NoMatch),
                        ro(api::routeMqtt("cmd/device/factory-reset", "", c, res)));
  TEST_ASSERT_EQUAL_INT(ro(api::RouteOutcome::NoMatch),
                        ro(api::routeMqtt("brightness", "120", c, res)));
  TEST_ASSERT_EQUAL_INT(ro(api::RouteOutcome::NoMatch),
                        ro(api::routeMqtt("notify", "{}", c, res)));
}

static void test_mqtt_bad_body_responds_error() {
  Command c;
  std::string res;
  TEST_ASSERT_EQUAL_INT(ro(api::RouteOutcome::Respond),
                        ro(api::routeMqtt("cmd/audio/play", "{bad", c, res)));
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
  const std::string e = api::errorJson("validationFailed", "out of range", "volume");
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
  TEST_ASSERT_EQUAL_STRING("volume", field("field").c_str());
}

static void test_mqtt_result_payloads() {
  DispatchDetail none;
  TEST_ASSERT_EQUAL_STRING("{\"ok\":true}", api::mqttResult(DispatchResult::Ok, none).c_str());
  DispatchDetail d{"volume", "out of range"};
  const std::string r = api::mqttResult(DispatchResult::ValidationError, d);
  TEST_ASSERT_TRUE(r.find("\"ok\":false") != std::string::npos);
  TEST_ASSERT_TRUE(r.find("validationFailed") != std::string::npos);
  TEST_ASSERT_TRUE(r.find("volume") != std::string::npos);
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
                        (int)api::routeMqtt("cmd/notify/dismiss/backup-job", "", c, res));
  TEST_ASSERT_EQUAL_INT((int)CommandType::DismissNotify, (int)c.type);
  TEST_ASSERT_EQUAL_STRING("backup-job", c.name.c_str());
  Command c2; std::string res2;
  api::routeMqtt("cmd/notify/dismiss", "", c2, res2);
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

int main(int, char**) {
  UNITY_BEGIN();
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
  RUN_TEST(test_http_indicators);
  RUN_TEST(test_http_sounds_play_variants);
  RUN_TEST(test_http_sounds_play_rejects_unparseable_rtttl);
  RUN_TEST(test_http_sounds_stop);
  RUN_TEST(test_mqtt_sounds_share_the_http_validation);
  RUN_TEST(test_http_device_actions);
  RUN_TEST(test_http_reads_and_unknown_no_match);
  RUN_TEST(test_http_get_only_reads_reject_other_methods);
  RUN_TEST(test_http_shared_state_is_read_only);
  RUN_TEST(test_app_name_validation);
  RUN_TEST(test_http_script_put_routes_with_source);
  RUN_TEST(test_http_script_traversal_name_rejected);
  RUN_TEST(test_http_delete_app_is_kind_agnostic);
  RUN_TEST(test_http_reserved_app_paths_are_not_names);
  RUN_TEST(test_http_script_oversize_source_rejected);
  RUN_TEST(test_http_script_empty_source_rejected);
  RUN_TEST(test_http_script_get_is_read_and_others_405);
  RUN_TEST(test_http_script_config_routes);
  RUN_TEST(test_http_script_config_is_claimed_before_the_catch_all);
  RUN_TEST(test_http_response_script_config_set_reports_error_state);
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
  RUN_TEST(test_script_write_is_exempt_from_the_json_gate);
  return UNITY_END();
}
