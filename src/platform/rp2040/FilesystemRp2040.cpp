#include "persistence/Filesystem.h"
#include <LittleFS.h>

namespace awtrix::fs {
bool begin() {
  // arduino-pico's LittleFS configuration formats an unmountable volume, matching
  // the ESP32 begin(true) policy. Formatting also erases corrupt volumes.
  LittleFSConfig config;
  config.setAutoFormat(true);
  LittleFS.setConfig(config);
  if (!LittleFS.begin()) return false;
  for (const char* dir : {"/ICONS", "/PALETTES", "/MELODIES", "/SCRIPTS", "/NVS"})
    if (!LittleFS.exists(dir) && !LittleFS.mkdir(dir)) return false;
  return true;
}
bool usage(std::size_t& totalBytes, std::size_t& usedBytes) {
  FSInfo info;
  const bool ok = LittleFS.info(info);
  totalBytes = ok ? info.totalBytes : 0;
  usedBytes = ok ? info.usedBytes : 0;
  return ok;
}
}
