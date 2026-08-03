#pragma once

#include <cstdint>

#include <esp_timer.h>

namespace awtrix {

// Milliseconds since boot in 64 bits. Unlike millis() this never wraps, so timestamps stored in the
// engine stay comparable past the 49-day mark.
inline int64_t monotonicMs() { return esp_timer_get_time() / 1000; }

}
