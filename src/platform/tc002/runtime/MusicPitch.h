#pragma once

#include <cstdint>

#include "core/audio/AnalysisSource.h"
#include "platform/tc002/audio/PlaybackPitch.h"
#include "platform/tc002/runtime/MicrophoneInput.h"

namespace awtrix::tc002 {

// music.pitch() from the source the music source setting selects, as audio::AnalysisRouter picks
// it for the other readings: Automatic takes playback while some of it was heard within the last
// 0.3 s, otherwise the microphone. The microphone is asked only when it is the one chosen.
inline float musicPitch(audio::AnalysisSource source, int64_t nowMs, PlaybackPitch* playback,
                        MicrophoneInput* microphone) {
  float hz = 0.f;
  if (source != audio::AnalysisSource::Microphone && playback && playback->pitch(nowMs, hz)) return hz;
  if (source != audio::AnalysisSource::Playback && microphone) return microphone->pitch(nowMs);
  return 0.f;
}

}
