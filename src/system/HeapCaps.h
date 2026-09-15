#pragma once

#include <esp_heap_caps.h>

namespace awtrix {

// The heap every low-memory check is made against. Internal RAM only: PSRAM is plentiful but
// unusable for DMA and interrupt code, so counting it would hide the shortage that actually bites.
constexpr uint32_t kGuardHeapCaps = MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT;

// The heap a script's larger working buffers come from: the uploaded source, a collected HTTP
// answer, a settings list, a serialised store. The framework serves anything past a few kilobytes
// from PSRAM where a board has it, so a guard on those buffers has to weigh that same pool.
// Everything DMA touches keeps to kGuardHeapCaps.
uint32_t scriptBufferHeapCaps();

}
