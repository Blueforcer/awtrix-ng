#include "persistence/Filesystem.h"

#include <LittleFS.h>
#include <esp_flash.h>
#include <esp_partition.h>

#include "system/Log.h"

namespace awtrix {
namespace fs {

namespace {
void ensureDir(const char* path) {
  if (!LittleFS.exists(path)) LittleFS.mkdir(path);
}

// Catches the classic wrong-image case: a build for a 8/16 MB board flashed onto a 4 MB chip.
// The mount usually still succeeds and only starts losing files once writes reach the gap.
void checkFlashFitsTable() {
  const esp_partition_t* p = esp_partition_find_first(
      ESP_PARTITION_TYPE_DATA, ESP_PARTITION_SUBTYPE_DATA_SPIFFS, nullptr);
  if (!p) return;

  uint32_t chip = 0;
  if (esp_flash_get_size(nullptr, &chip) != ESP_OK || chip == 0) return;

  const uint32_t end = p->address + p->size;
  if (end <= chip) return;

  logf("fs: partition table needs %u KB of flash, this chip has %u KB - storage runs "
       "%u KB past the end. Flash the factory image built for this board.",
       (unsigned)(end / 1024), (unsigned)(chip / 1024), (unsigned)((end - chip) / 1024));
}
}

bool begin() {
  checkFlashFitsTable();
  // The true formats on a failed mount, which is what a freshly flashed board needs.
  if (!LittleFS.begin(true)) return false;
  ensureDir("/ICONS");
  ensureDir("/PALETTES");
  ensureDir("/MELODIES");
  ensureDir("/SCRIPTS");
  return true;
}

}
}
