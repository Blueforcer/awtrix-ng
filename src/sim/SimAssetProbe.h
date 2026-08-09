#pragma once

#include <filesystem>
#include <string>

#include "core/sound/AudioSinks.h"
#include "core/sound/SoundMp3.h"
#include "sim/SimStore.h"

namespace awtrix {

// The host half of LittleFsAssetProbe.
class SimAssetProbe : public sound::IAssetProbe {
 public:
  bool hasMp3(const std::string& name) const override {
    const std::string path = sound::mp3PathFor(name);
    if (path.empty()) return false;
    return std::filesystem::exists(std::filesystem::u8path(sim::hostPath(path)));
  }
};

}
