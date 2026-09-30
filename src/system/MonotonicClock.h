#pragma once

#include <cstdint>

#if defined(AWTRIX_PLATFORM_RP2040)
#include <pico/time.h>
#else
#include <esp_timer.h>
#endif

namespace awtrix {

// Milliseconds since boot in 64 bits. Unlike millis() this never wraps, so timestamps stored in the
// engine stay comparable past the 49-day mark.
inline int64_t monotonicMs() {
#if defined(AWTRIX_PLATFORM_RP2040)
  return time_us_64() / 1000;
#else
  return esp_timer_get_time() / 1000;
#endif
}

}
