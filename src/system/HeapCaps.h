#pragma once

#include <esp_heap_caps.h>

namespace awtrix {

// The heap every low-memory check is made against. Internal RAM only: PSRAM is plentiful but
// unusable for DMA and interrupt code, so counting it would hide the shortage that actually bites.
constexpr uint32_t kGuardHeapCaps = MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT;

}
