#pragma once

#include <cstdint>
#include <memory>
#include <string>

#include "core/CoreEngine.h"
#include "core/Settings.h"
#include "core/sound/AudioRouter.h"
#include "core/sound/AudioSettings.h"
#include "platform/tc002/audio/Tc002AudioSink.h"
#include "platform/tc002/audio/AudioClip.h"
#include "platform/tc002/contract/RuntimeContract.h"

namespace awtrix {

// The TC002 speaker: the awtrix-tc002-audio-pcm helper on an inherited socket, as tone and PCM sink,
// and with a voice the one that speaks.
class Tc002Speaker : public audio::IAnalysisSource {
 public:
  static constexpr int kDescriptor = tc002::kAudioFd;

  Tc002Speaker(CoreEngine& engine, sound::AudioRouter& router, int descriptor,
               std::shared_ptr<speech::SpeechVoice> voice = {})
      : engine_(engine), router_(router), sink_(engine, descriptor, std::move(voice)) {
    router_.setTone(&sink_);
    router_.setPcm(&sink_);
    engine_.setPcmSink(&sink_);
  }
  ~Tc002Speaker() {
    router_.setTone(nullptr);
    router_.setPcm(nullptr);
    engine_.setPcmSink(nullptr);
  }
  Tc002Speaker(const Tc002Speaker&) = delete;
  Tc002Speaker& operator=(const Tc002Speaker&) = delete;

  bool songBeat(int64_t now, double& beat) const { return sink_.songBeat(now, beat); }
  int routeAudio(const std::string& method, const std::string& path, const std::string& request,
                 std::string& body) { return tc002::routeClip(method, path, request, body, &sink_, router_); }

  bool available() const { return sink_.available(); }
  bool analysis(int64_t nowMs, audio::FrameStats& out) override { return sink_.analysis(nowMs, out); }
  tc002::PlaybackPitch& playbackPitch() { return sink_.playbackPitch(); }

  // The boot sound as an alert, only while the boot sound is on; false when nothing will play.
  bool playBootSound(const std::string& path, uint32_t leadFrames, const Settings& settings) {
    return settings.bootSound && sink_.playSystemSound(path, leadFrames);
  }
  tc002::Tc002AudioSink::SystemStart bootSoundStart(int64_t& audibleAtMs) const {
    return sink_.systemStart(audibleAtMs);
  }
  void stopBootSound() { sink_.stopSystemSound(); }
  void setVoiceOwned(bool owned) { sink_.setVoiceOwned(owned); }
  bool playVoice(const std::string& path) { return sink_.playSystemSound(path, 0); }
  void stopVoice() { sink_.stopSystemSound(); }
  bool voiceFinished() const { return sink_.systemFinished(); }
  bool voiceQuiet() const { return sink_.voiceQuiet(); }

 private:
  CoreEngine& engine_;
  sound::AudioRouter& router_;
  tc002::Tc002AudioSink sink_;
};

}
