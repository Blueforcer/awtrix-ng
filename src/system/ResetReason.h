#pragma once
#if defined(AWTRIX_PLATFORM_RP2040)
#include <Arduino.h>
#include "system/PicoResetReason.h"
#else
#include <esp_system.h>
#endif

namespace awtrix::platform {
inline const char* resetReasonName() {
#if defined(AWTRIX_PLATFORM_RP2040)
  return picoResetReasonName(rp2040.getResetReason());
#else
  switch (esp_reset_reason()) {
    case ESP_RST_POWERON: return "poweron";
    case ESP_RST_EXT: return "external";
    case ESP_RST_SW: return "software";
    case ESP_RST_PANIC: return "panic";
    case ESP_RST_INT_WDT: return "interruptWatchdog";
    case ESP_RST_TASK_WDT: return "taskWatchdog";
    case ESP_RST_WDT: return "watchdog";
    case ESP_RST_DEEPSLEEP: return "deepSleep";
    case ESP_RST_BROWNOUT: return "brownout";
    case ESP_RST_SDIO: return "sdio";
    default: return "unknown";
  }
#endif
}
}
