
#include <filesystem>
#include <fstream>
#include <limits>

#include "media/AssetFile.h"
#include "sim/SimStore.h"

namespace awtrix {
namespace media {

// Read into the checked buffer directly, avoiding a second copy of the complete file.
bool readAsset(const std::string& path, PodBuffer<uint8_t>& out, bool* outOfMemory) {
  if (outOfMemory) *outOfMemory = false;
  std::ifstream file(std::filesystem::u8path(sim::hostPath(path)), std::ios::binary | std::ios::ate);
  if (!file) return false;
  const std::streamoff length = file.tellg();
  if (length <= 0 || static_cast<uintmax_t>(length) > std::numeric_limits<size_t>::max() ||
      length > std::numeric_limits<std::streamsize>::max()) return false;
  const size_t size = static_cast<size_t>(length);
  if (!out.resize(size)) {
    if (outOfMemory) *outOfMemory = true;
    return false;
  }
  file.seekg(0);
  file.read(reinterpret_cast<char*>(out.data()), static_cast<std::streamsize>(size));
  if (file.gcount() != static_cast<std::streamsize>(size)) {
    out.clear();
    return false;
  }
  return true;
}

}
}
