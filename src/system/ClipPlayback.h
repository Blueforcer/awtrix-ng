#pragma once

#include <LittleFS.h>

#include <string>

#include "core/Services.h"
#include "core/sound/SoundClips.h"

namespace awtrix {

// A stored MP3 clip wins over a melody of the same name. False means there is no such clip - on a
// board without an I2S output there is no clip service at all - and the caller falls back to the
// buzzer.
inline bool playStoredClip(IClipService* clips, const std::string& name) {
  if (!clips) return false;
  const std::string path = sound::clipPathFor(name);
  if (path.empty() || !LittleFS.exists(path.c_str())) return false;
  return clips->playClip(path);
}

}
