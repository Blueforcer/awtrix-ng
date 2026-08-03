
#include <cstring>

#include "media/AssetFile.h"
#include "sim/SimStore.h"

namespace awtrix {
namespace media {

// Host half of the asset reader; the device half in media/AssetFileDevice.cpp reads from SPIFFS.
bool readAsset(const std::string& path, PodBuffer<uint8_t>& out) {
  std::string bytes;
  if (!sim::readFile(sim::hostPath(path), bytes) || bytes.empty()) return false;
  if (!out.resize(bytes.size())) return false;
  std::memcpy(out.data(), bytes.data(), bytes.size());
  return true;
}

}
}
