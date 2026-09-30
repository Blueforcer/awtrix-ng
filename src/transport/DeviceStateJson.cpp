#include "transport/DeviceStateJson.h"

#include <Arduino.h>
#if defined(AWTRIX_PLATFORM_RP2040)
#include <malloc.h>
#include <unistd.h>
#else
#include <esp_heap_caps.h>
#endif
#include <WiFi.h>
#include "system/ResetReason.h"

#include <algorithm>
#include <cmath>

#include "AppConfig.h"
#include "core/CoreEngine.h"
#include "core/SocProfile.h"
#include "core/render/Color.h"
#include "hal/IBoard.h"
#if !defined(AWTRIX_PLATFORM_RP2040)
#include "system/HeapCaps.h"
#endif
#include "system/MonotonicClock.h"
#include "transport/http/UpdateImage.h"

namespace awtrix {

namespace {
const char* kBoardType = "awtrixng";

}

std::string buildDeviceStateJson(CoreEngine& engine, IBoard& board, const std::string& uid,
                                 bool scriptingRunning) {
  ISensorBus& sensors = board.sensors();
  DeviceFacts facts;
  facts.boardType = kBoardType;
  facts.soc = pins::activeProfile().id;
  facts.updateImage = kUpdateImageName;
  facts.ipAddress = std::string(WiFi.localIP().toString().c_str());
  const char* hn = WiFi.getHostname();
  facts.hostname = hn ? hn : "";
  facts.wifiRssi = WiFi.RSSI();
  facts.uptimeSeconds = static_cast<long>(monotonicMs() / 1000);
  // Internal RAM only. PSRAM is reported separately, because it is the internal heap that runs out
  // first and it is the number worth watching.
#if defined(AWTRIX_PLATFORM_RP2040)
  facts.freeHeapBytes = rp2040.getFreeHeap();
  // arduino-pico has no low-water API; this low water is sampled by device requests.
  static uint32_t sampledMin = facts.freeHeapBytes;
  sampledMin = std::min(sampledMin, facts.freeHeapBytes);
  facts.minFreeHeapBytes = sampledMin;
  // newlib cannot walk its free list, so this is the block at the top of the heap: the free chunk
  // malloc keeps there plus what sbrk has not handed out yet, up to the stack. Holes freed further
  // down are not counted, so the true largest block is at least this, less malloc's page rounding.
  const ptrdiff_t unclaimed = &__StackLimit - static_cast<char*>(sbrk(0));
  facts.largestFreeBlockBytes = static_cast<uint32_t>(mallinfo().keepcost) +
                                static_cast<uint32_t>(std::max<ptrdiff_t>(unclaimed, 0));
#else
  facts.freeHeapBytes = heap_caps_get_free_size(kGuardHeapCaps);
  facts.minFreeHeapBytes = heap_caps_get_minimum_free_size(kGuardHeapCaps);
  facts.largestFreeBlockBytes = heap_caps_get_largest_free_block(kGuardHeapCaps);
  facts.psramTotalBytes = heap_caps_get_total_size(MALLOC_CAP_SPIRAM);
  facts.psramFreeBytes = heap_caps_get_free_size(MALLOC_CAP_SPIRAM);
#endif
  facts.resetReason = platform::resetReasonName();
  facts.hasBattery = board.hasBattery();
  facts.hasLightSensor = board.hasLightSensor();
  facts.hasTemperature = sensors.hasSensor();
  facts.hasHumidity = sensors.hasHumidity();
  facts.hasPressure = sensors.hasPressure();
  facts.scriptingRunning = scriptingRunning;
  return buildDeviceJson(engine, uid, facts);
}

}
