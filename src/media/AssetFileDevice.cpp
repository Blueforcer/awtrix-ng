
#include <LittleFS.h>

#include "media/AssetFile.h"

namespace awtrix {
namespace media {

bool readAsset(const std::string& path, PodBuffer<uint8_t>& out) {
  File f = LittleFS.open(path.c_str(), "r");
  if (!f) return false;
  const size_t n = f.size();
  if (n == 0 || !out.resize(n)) {
    f.close();
    return false;
  }
  f.read(out.data(), n);
  f.close();
  return true;
}

}
}
