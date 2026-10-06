#pragma once

#include "core/Settings.h"
#include "core/sound/AudioRouter.h"

namespace awtrix {

inline void applyAudioSettings(sound::AudioRouter& audio, const Settings& settings) {
  audio.setVolumes(settings.volume, settings.radioVolume, settings.appVolume, settings.alertVolume);
}

}
