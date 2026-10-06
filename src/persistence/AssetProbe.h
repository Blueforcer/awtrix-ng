#pragma once

#include <string>

#include "core/sound/AudioSinks.h"
#include "persistence/VfsFile.h"

namespace awtrix {

class AssetProbe : public sound::IAssetProbe {
 public:
  bool hasFile(const std::string& path) const override { return fs::isFile(path); }
};

}
