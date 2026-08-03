#include "transport/DeviceStateJson.h"

#include <Arduino.h>
#include <esp_heap_caps.h>
#include <WiFi.h>
#include <esp_system.h>

#include <cmath>

#include "AppConfig.h"
#include "core/CoreEngine.h"
#include "core/SocProfile.h"
#include "core/render/Color.h"
#include "hal/IBoard.h"
#include "system/HeapCaps.h"
#include "system/MonotonicClock.h"

namespace awtrix {

namespace {
const char* kBoardType = "awtrixng";

const char* resetReasonName() {
  switch (esp_reset_reason()) {
    case ESP_RST_POWERON:  return "poweron";
    case ESP_RST_EXT:      return "external";
    case ESP_RST_SW:       return "software";
    case ESP_RST_PANIC:    return "panic";
    case ESP_RST_INT_WDT:  return "interruptWatchdog";
    case ESP_RST_TASK_WDT: return "taskWatchdog";
    case ESP_RST_WDT:      return "watchdog";
    case ESP_RST_DEEPSLEEP: return "deepSleep";
    case ESP_RST_BROWNOUT: return "brownout";
    case ESP_RST_SDIO:     return "sdio";
    default:               return "unknown";
  }
}

}

std::string buildDeviceStateJson(CoreEngine& engine, IBoard& board, const std::string& uid,
                                 bool scriptingRunning) {
  ISensorBus& sensors = board.sensors();
  DeviceFacts facts;
  facts.boardType = kBoardType;
  facts.soc = pins::activeProfile().id;
  facts.ipAddress = std::string(WiFi.localIP().toString().c_str());
  const char* hn = WiFi.getHostname();
  facts.hostname = hn ? hn : "";
  facts.wifiRssi = WiFi.RSSI();
  facts.uptimeSeconds = static_cast<long>(monotonicMs() / 1000);
  // Internal RAM only. PSRAM is reported separately, because it is the internal heap that runs out
  // first and it is the number worth watching.
  facts.freeHeapBytes = heap_caps_get_free_size(kGuardHeapCaps);
  facts.minFreeHeapBytes = heap_caps_get_minimum_free_size(kGuardHeapCaps);
  facts.largestFreeBlockBytes = heap_caps_get_largest_free_block(kGuardHeapCaps);
  facts.psramTotalBytes = heap_caps_get_total_size(MALLOC_CAP_SPIRAM);
  facts.psramFreeBytes = heap_caps_get_free_size(MALLOC_CAP_SPIRAM);
  facts.resetReason = resetReasonName();
  facts.hasBattery = board.hasBattery();
  facts.hasLightSensor = board.hasLightSensor();
  facts.hasTemperature = sensors.hasSensor();
  facts.hasHumidity = sensors.hasHumidity();
  facts.hasPressure = sensors.hasPressure();
  facts.scriptingRunning = scriptingRunning;
  return buildDeviceJson(engine, uid, facts);
}

}
