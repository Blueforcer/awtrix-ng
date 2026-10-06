#pragma once

#include <cstdint>
#include <esp_timer.h>

namespace awtrix {
inline int64_t monotonicUs() { return esp_timer_get_time(); }
inline int64_t monotonicMs() { return monotonicUs() / 1000; }
}
