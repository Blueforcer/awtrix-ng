#pragma once

#include <cstdlib>


#if defined(ESP_PLATFORM)

#include <esp_heap_caps.h>
#include <esp_idf_version.h>
#include <esp_system.h>

namespace awtrix::media::heap {

// Classic ESP32 silicon before rev 3.0 has the PSRAM cache errata, and the workaround costs more
// than the memory is worth here — treat that PSRAM as absent. Probed once, then cached.
inline bool psramUsable() {
  static const bool usable = [] {
    if (heap_caps_get_free_size(MALLOC_CAP_SPIRAM) == 0) return false;
#if defined(CONFIG_IDF_TARGET_ESP32)
    esp_chip_info_t chip;
    esp_chip_info(&chip);
    // Older IDF reported the bare major revision, newer ones major*100 + minor. Normalise both
    // to the hundreds form before comparing.
    unsigned revision = chip.revision;
    if (revision < 100) revision *= 100;
    if (chip.model == CHIP_ESP32 && revision < 300) return false;
#endif
    return true;
  }();
  return usable;
}

inline void* acquire(std::size_t bytes) {
  if (psramUsable()) {
    void* p = heap_caps_malloc(bytes, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (p) return p;
  }
  return std::malloc(bytes);
}

inline void release(void* p) { std::free(p); }

}

#else

#include <new>

namespace awtrix::media::heap {

inline void* acquire(std::size_t bytes) { return new (std::nothrow) unsigned char[bytes]; }
inline void release(void* p) { delete[] static_cast<unsigned char*>(p); }

}

#endif
