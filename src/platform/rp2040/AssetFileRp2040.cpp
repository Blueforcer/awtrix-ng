#include <LittleFS.h>

#include "media/AssetFile.h"

namespace awtrix {
namespace media {

// The ESP32 reads assets through its VFS; arduino-pico has none, so use the LittleFS File API.
bool readAsset(const std::string& path, PodBuffer<uint8_t>& out, bool* outOfMemory) {
  if (outOfMemory) *outOfMemory = false;
  File f = LittleFS.open(path.c_str(), "r");
  if (!f || f.isDirectory() || f.size() == 0) return false;
  const std::size_t n = f.size();
  if (!out.resize(n)) {
    if (outOfMemory) *outOfMemory = true;
    return false;
  }
  if (f.read(out.data(), n) != n) {
    out.clear();
    return false;
  }
  return true;
}

}
}
