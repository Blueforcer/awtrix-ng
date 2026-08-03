#pragma once


#if defined(ESP_PLATFORM)

#include <esp_heap_caps.h>
#include <esp_system.h>

namespace awtrix {

// Allocations at or above this go to PSRAM. Small ones stay internal, where the latency difference
// matters and where DMA-capable memory is needed anyway.
constexpr std::size_t kExtMemThresholdBytes = 4096;

// ESP32 silicon before revision 3 has the PSRAM cache errata; the workaround makes routing plain
// malloc into PSRAM unsafe, so those chips stay on internal RAM whatever the config says.
inline bool psramSteerable() {
  if (heap_caps_get_free_size(MALLOC_CAP_SPIRAM) == 0) return false;
#if defined(CONFIG_IDF_TARGET_ESP32)
  esp_chip_info_t chip;
  esp_chip_info(&chip);
  unsigned revision = chip.revision;
  if (revision < 100) revision *= 100;
  if (chip.model == CHIP_ESP32 && revision < 300) return false;
#endif
  return true;
}

inline bool steerLargeAllocationsToPsram() {
  if (!psramSteerable()) return false;
  heap_caps_malloc_extmem_enable(kExtMemThresholdBytes);
  return true;
}

}

#else

namespace awtrix {
inline bool steerLargeAllocationsToPsram() { return false; }
}

#endif
