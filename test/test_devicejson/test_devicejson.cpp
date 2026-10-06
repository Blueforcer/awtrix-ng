#include "../EngineFakes.h"
#include <unity.h>

#include <string>

#include "core/CoreEngine.h"
#include "core/api/JsonWriter.h"
#include "core/api/StateJson.h"

using namespace awtrix;

namespace {
using FDisplay = awtrix::test::NullDisplay;
using FSystem = awtrix::test::NullSystem;

struct Fixture {
  sound::AudioRouter audio;
  FDisplay display;
  FSystem system;
  CoreEngine engine{audio, display, system};

  Fixture() {
    RuntimeState& rt = engine.state().runtime();
    rt.fps = 40;
    rt.brightnessActual = 120;
    rt.batteryPercent = 87;
    rt.batteryVoltage = 4.05f;
    rt.batteryPinMillivolts = 2262;
    rt.lightLevel = 12.34f;
    rt.ldrRaw = 812;
    rt.temperatureC = 21.46f;
    rt.humidity = 40.04f;
  }
};

DeviceFacts esp32Facts() {
  DeviceFacts facts;
  facts.boardType = "awtrixng";
  facts.soc = "esp32";
  facts.updateImage = "firmware.bin";
  facts.ipAddress = "192.168.1.50";
  facts.macAddress = {0xa1, 0xb2, 0xc3, 0xd4, 0xe5, 0xf6};
  facts.hasMacAddress = true;
  facts.hostname = "awtrix-abc123";
  facts.wifiRssi = -61;
  facts.uptimeSeconds = 1234;
  facts.freeHeapBytes = 118234;
  facts.minFreeHeapBytes = 91560;
  facts.largestFreeBlockBytes = 63488;
  facts.hasLargestFreeBlock = true;
  facts.resetReason = "poweron";
  facts.hasBattery = true;
  facts.hasBatteryDivider = true;
  facts.hasLightSensor = true;
  facts.hasTemperature = true;
  facts.hasHumidity = true;
  facts.scriptingRunning = true;
  return facts;
}

const std::string kLinks =
    R"("messageCount":0,"wifi":{"enabled":false,"state":"disabled","host":"","endpoint":"",)"
    R"("attempts":0,"retryInMs":0,"connects":0,"error":null,"lastError":null},)"
    R"("mqtt":{"enabled":false,"state":"disabled","host":"","endpoint":"","attempts":0,)"
    R"("retryInMs":0,"connects":0,"error":null,"lastError":null},)"
    R"("mirror":{"sharing":false,"viewers":0,"source":"","state":"off"})";
const std::string kIndicators =
    R"("indicators":[{"on":false,"color":"#000000","blinkMs":0,"fadeMs":0},)"
    R"({"on":false,"color":"#000000","blinkMs":0,"fadeMs":0},)"
    R"({"on":false,"color":"#000000","blinkMs":0,"fadeMs":0}],)";
}

void setUp() {}
void tearDown() {}

// The ESP32 device document.
static void test_esp32_document_is_unchanged() {
  Fixture f;
  const std::string expected =
      std::string(R"({"version":")") + AWTRIX_NG_VERSION +
      R"(","uid":"a1b2c3d4e5f6","boardType":"awtrixng","soc":"esp32",)"
      R"("updateImage":"firmware.bin","ipAddress":"192.168.1.50","macAddress":"A1:B2:C3:D4:E5:F6",)"
      R"("hostname":"awtrix-abc123",)"
      R"("wifiRssi":-61,"uptimeSeconds":1234,"freeHeapBytes":118234,"minFreeHeapBytes":91560,)"
      R"("largestFreeBlockBytes":63488,"scriptingRunning":true,"scriptHeapPool":"internal",)"
      R"("scriptHeapBudgetBytes":98304,"resetReason":"poweron","fps":40,"brightness":120,)"
      R"("lightLevel":12.3,"ldrRaw":812,"batteryPercent":87,"batteryVoltage":4.05,)"
      R"("batteryPinMillivolts":2262,"lowBattery":false,"temperature":21.5,"humidity":40,)"
      R"("matrixPower":true,"currentApp":"Time",)" + kIndicators + kLinks + "}";
  TEST_ASSERT_EQUAL_STRING(expected.c_str(),
                           buildDeviceJson(f.engine, "a1b2c3d4e5f6", esp32Facts()).c_str());
}

static void test_members_a_platform_cannot_measure_are_left_out() {
  Fixture f;
  DeviceFacts facts = esp32Facts();
  facts.hasMacAddress = false;
  facts.hasLargestFreeBlock = false;
  facts.hasBatteryDivider = false;
  const std::string json = buildDeviceJson(f.engine, "a1b2c3d4e5f6", facts);
  TEST_ASSERT_EQUAL(std::string::npos, json.find("macAddress"));
  TEST_ASSERT_NOT_EQUAL(std::string::npos, json.find(R"("ipAddress":"192.168.1.50","hostname")"));
  TEST_ASSERT_EQUAL(std::string::npos, json.find("largestFreeBlockBytes"));
  TEST_ASSERT_EQUAL(std::string::npos, json.find("batteryPinMillivolts"));
  TEST_ASSERT_NOT_EQUAL(std::string::npos, json.find(R"("minFreeHeapBytes":91560,"scriptingRunning")"));
  TEST_ASSERT_NOT_EQUAL(std::string::npos, json.find(R"("batteryVoltage":4.05,"lowBattery")"));
}

static void test_platform_members_follow_the_shared_ones() {
  Fixture f;
  const std::string json = buildDeviceJson(f.engine, "a1b2c3d4e5f6", esp32Facts(),
      [](api::JsonWriter& w) { w.key("update").beginObject().member("state", "idle").endObject(); });
  const std::string tail = R"("state":"off"},"update":{"state":"idle"}})";
  TEST_ASSERT_TRUE(json.size() > tail.size());
  TEST_ASSERT_EQUAL_STRING(tail.c_str(), json.substr(json.size() - tail.size()).c_str());
}

static void test_mirror_status_reports_the_followed_clock() {
  Fixture f;
  mirror::Status& status = f.engine.state().runtime().mirror;
  status.sharing = true;
  status.viewers = 2;
  status.source = "kitchen.local";
  status.follow = mirror::FollowState::SizeMismatch;
  status.sourceWidth = 52;
  status.sourceHeight = 16;
  const std::string json = buildDeviceJson(f.engine, "a1b2c3d4e5f6", esp32Facts());
  TEST_ASSERT_NOT_EQUAL(std::string::npos,
      json.find(R"("mirror":{"sharing":true,"viewers":2,"source":"kitchen.local",)"
                R"("state":"sizeMismatch","sourceWidth":52,"sourceHeight":16})"));
}

int main() {
  UNITY_BEGIN();
  RUN_TEST(test_esp32_document_is_unchanged);
  RUN_TEST(test_members_a_platform_cannot_measure_are_left_out);
  RUN_TEST(test_mirror_status_reports_the_followed_clock);
  RUN_TEST(test_platform_members_follow_the_shared_ones);
  return UNITY_END();
}
