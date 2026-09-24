#include <unity.h>
#include <cstdio>
#include "core/PinRules.h"
#include "core/SocProfileJson.h"
#include "core/api/ApiRouter.h"
#include "core/api/CapabilitiesJson.h"
#include "platform/BuildFeatures.h"
using namespace awtrix;
void setUp() {}
void tearDown() {}
static constexpr FeatureSet pico{false, false, false, false, false};

static void http_profiles() {
  struct Request { const char* method; const char* path; const char* body; };
  const Request requests[] = {
      {"GET", "/api/v1/scripts/shared", ""},
      {"GET", "/api/v1/apps/script/demo", ""},
      {"PUT", "/api/v1/apps/script/demo", "print(1)"},
      {"PUT", "/api/v1/apps/script-update/demo", "{}"},
      {"GET", "/api/v1/apps/demo/config", ""},
      {"PATCH", "/api/v1/apps/demo/config", "{}"},
      {"GET", "/api/v1/audio/mp3", ""},
      {"POST", "/api/v1/audio/mp3", "binary"},
      {"DELETE", "/api/v1/audio/mp3/demo.mp3", ""},
      {"GET", "/api/v1/audio/stations", ""},
      {"PUT", "/api/v1/audio/stations", "{\"stations\":[]}"},
      {"POST", "/api/v1/audio/play", "{\"mp3\":\"demo\"}"},
      {"POST", "/api/v1/audio/play", "{\"station\":\"demo\"}"},
      {"POST", "/api/v1/audio/play", "{\"url\":\"https://example.com/live\"}"},
      {"POST", "/api/v1/audio/play", "{\"index\":0}"},
      {"GET", "/update", ""}, {"POST", "/update", "binary"},
  };
  for (const auto& req : requests) {
    Command cmd;
    api::HttpResult result;
    TEST_ASSERT_EQUAL_INT(static_cast<int>(api::RouteOutcome::Respond),
        static_cast<int>(api::routeHttp(req.method, req.path, req.body, cmd, result, pico)));
    TEST_ASSERT_EQUAL_INT(503, result.status);
    TEST_ASSERT_TRUE(result.body.find("\"code\":\"unavailable\"") != std::string::npos);
    api::HttpResult baseline, explicitEsp;
    Command original;
    const auto before = api::routeHttp(req.method, req.path, req.body, original, baseline);
    const auto after = api::routeHttp(req.method, req.path, req.body, cmd, explicitEsp, FeatureSet{});
    TEST_ASSERT_EQUAL_INT(static_cast<int>(before), static_cast<int>(after));
    TEST_ASSERT_EQUAL_INT(baseline.status, explicitEsp.status);
    TEST_ASSERT_EQUAL_STRING(baseline.body.c_str(), explicitEsp.body.c_str());
    TEST_ASSERT_EQUAL_INT(static_cast<int>(original.type), static_cast<int>(cmd.type));
    TEST_ASSERT_NOT_EQUAL(503, explicitEsp.status);
  }
  Command cmd;
  api::HttpResult result;
  TEST_ASSERT_EQUAL_INT(static_cast<int>(api::RouteOutcome::Routed),
      static_cast<int>(api::routeHttp("POST", "/api/v1/device/reboot", "", cmd, result, pico)));
}

static void mqtt_profiles() {
  const char* payloads[] = {"{\"mp3\":\"demo\"}", "{\"station\":\"demo\"}",
      "{\"index\":0}", "{\"url\":\"https://example.com/live\"}"};
  for (const auto payload : payloads) {
    Command cmd;
    std::string result;
    TEST_ASSERT_EQUAL_INT(static_cast<int>(api::RouteOutcome::Respond),
        static_cast<int>(api::routeMqtt("cmd/audio/play", payload, cmd, result, pico)));
    TEST_ASSERT_EQUAL_STRING(api::mqttResult(DispatchResult::Unavailable, {}).c_str(), result.c_str());
    TEST_ASSERT_EQUAL_INT(static_cast<int>(api::RouteOutcome::Routed),
        static_cast<int>(api::routeMqtt("cmd/audio/play", payload, cmd, result, FeatureSet{})));
  }
  Command cmd;
  std::string result;
  TEST_ASSERT_EQUAL_INT(static_cast<int>(api::RouteOutcome::Respond),
      static_cast<int>(api::routeMqtt("cmd/audio/stations", "{}", cmd, result, pico)));
  TEST_ASSERT_EQUAL_STRING(api::mqttResult(DispatchResult::Unavailable, {}).c_str(), result.c_str());
  TEST_ASSERT_TRUE(api::isResultEcho("cmd/audio/stations/result"));
  // No MQTT script command exists in the API; do not invent one for Pico.
  TEST_ASSERT_EQUAL_INT(static_cast<int>(api::RouteOutcome::NoMatch),
      static_cast<int>(api::routeMqtt("cmd/apps/script/demo", "x", cmd, result, pico)));
  FeatureSet noTls;
  noTls.outboundTls = false;
  api::HttpResult response;
  TEST_ASSERT_EQUAL_INT(static_cast<int>(api::RouteOutcome::Respond),
      static_cast<int>(api::routeHttp("POST", "/api/v1/audio/play",
          "{\"url\":\"https://example.com/live\"}", cmd, response, noTls)));
  TEST_ASSERT_EQUAL_INT(503, response.status);
  TEST_ASSERT_EQUAL_INT(static_cast<int>(api::RouteOutcome::Routed),
      static_cast<int>(api::routeHttp("POST", "/api/v1/audio/play",
          "{\"url\":\"http://example.com/live\"}", cmd, response, noTls)));
}

static void fixed_pins() {
  const auto& profile = pins::rp2040Profile();
  std::string error;
  TEST_ASSERT_TRUE(pins::validate(profile.defaults, profile, error));
  int pins::PinSet::* const fields[] = {&pins::PinSet::matrix, &pins::PinSet::btnLeft,
      &pins::PinSet::btnSelect, &pins::PinSet::btnRight, &pins::PinSet::battery,
      &pins::PinSet::ldr, &pins::PinSet::buzzer, &pins::PinSet::i2cSda,
      &pins::PinSet::i2cScl, &pins::PinSet::dfRx, &pins::PinSet::dfTx,
      &pins::PinSet::i2sBclk, &pins::PinSet::i2sLrclk, &pins::PinSet::i2sDout,
      &pins::PinSet::i2sMclk, &pins::PinSet::ampEnable};
  for (auto field : fields) {
    auto changed = profile.defaults;
    ++(changed.*field);
    TEST_ASSERT_FALSE(pins::validate(changed, profile, error));
  }
  auto changed = profile.defaults;
  changed.dfplayerEnabled = true;
  TEST_ASSERT_FALSE(pins::validate(changed, profile, error));
  TEST_ASSERT_TRUE(pins::validate(pins::esp32Profile().defaults, pins::esp32Profile(), error));
  TEST_ASSERT_TRUE(pins::validate(pins::esp32s3Profile().defaults, pins::esp32s3Profile(), error));
}

static void capabilities() {
  sound::AudioRouter audio;
  const auto json = api::capabilitiesJson({}, {}, {}, audio.caps(), pico, pins::rp2040Profile());
  TEST_ASSERT_TRUE(json.find("\"scripting\":false") != std::string::npos);
  TEST_ASSERT_TRUE(json.find("\"scriptUpdates\":false") != std::string::npos);
  TEST_ASSERT_TRUE(json.find("\"buzzer\":false,\"track\":false,\"mp3\":false,\"radio\":false") != std::string::npos);
  TEST_ASSERT_TRUE(api::capabilitiesJson({}, {}, {}, audio.caps()).find("\"scripting\":true") != std::string::npos);
  // The pins are the board's, and the web UI shows them rather than offering them.
  TEST_ASSERT_TRUE(json.find("\"label\":\"Galactic Unicorn (Pico W / Pico 2 W)\",\"fixed\":true,") !=
                   std::string::npos);
  TEST_ASSERT_TRUE(json.find("\"pinI2cSda\":-1,\"pinI2cScl\":-1,") != std::string::npos);
  TEST_ASSERT_TRUE(pins::toJson(pins::esp32Profile()).find("\"fixed\":false,") != std::string::npos);
  std::printf("PICO_CAPABILITIES=%s\n", json.c_str());
}
int main() {
  UNITY_BEGIN();
  RUN_TEST(http_profiles);
  RUN_TEST(mqtt_profiles);
  RUN_TEST(fixed_pins);
  RUN_TEST(capabilities);
  return UNITY_END();
}
