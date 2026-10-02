#pragma once
#include <cstdint>
#if defined(AWTRIX_PLATFORM_RP2040)
#include <Arduino.h>
#include <hardware/structs/watchdog.h>
#include <hardware/watchdog.h>
#endif

namespace awtrix::watchdog {
// The Pico has no task watchdog: without this a hang or hard fault freezes the panel until someone
// power-cycles it, where an ESP32 reboots itself. ESP32 and the simulator keep their own behaviour,
// so every call below is a no-op there.
constexpr uint32_t kTimeoutMs = 8000; // RP2040 maximum is ~8.3 s
#if defined(AWTRIX_PLATFORM_RP2040)
inline void begin() { rp2040.wdt_begin(kTimeoutMs); }
inline void feed() { rp2040.wdt_reset(); }
// For work that ends in a reboot anyway and can outlast the timeout (formatting LittleFS). A later
// rp2040.reboot() re-arms the hardware itself, and still reads back as "software".
inline void stop() { hw_clear_bits(&watchdog_hw->ctrl, WATCHDOG_CTRL_ENABLE_BITS); }
#else
inline void begin() {}
inline void feed() {}
inline void stop() {}
#endif
}
