#pragma once

#include <LittleFS.h>

#include <string>

#include "core/sound/AudioSinks.h"
#include "core/sound/SoundMp3.h"

namespace awtrix {

class LittleFsAssetProbe : public sound::IAssetProbe {
 public:
  bool hasMp3(const std::string& name) const override {
    const std::string path = sound::mp3PathFor(name);
    return !path.empty() && LittleFS.exists(path.c_str());
  }
};

}
