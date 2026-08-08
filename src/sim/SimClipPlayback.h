#pragma once

#include <filesystem>
#include <string>

#include "core/Services.h"
#include "core/sound/SoundClips.h"
#include "sim/SimStore.h"

namespace awtrix {

// The host half of playStoredClip(): same policy, the standard filesystem instead of LittleFS.
inline bool playStoredClip(IClipService* clips, const std::string& name) {
  if (!clips) return false;
  const std::string path = sound::clipPathFor(name);
  if (path.empty() ||
      !std::filesystem::exists(std::filesystem::u8path(sim::hostPath(path))))
    return false;
  return clips->playClip(path);
}

}
