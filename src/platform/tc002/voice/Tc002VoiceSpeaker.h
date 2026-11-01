#pragma once

#include "platform/tc002/runtime/Tc002Speaker.h"
#include "platform/tc002/voice/VoiceSpeaker.h"

namespace awtrix::tc002::voice {

class Tc002VoiceSpeaker final : public VoiceSpeaker {
 public:
  explicit Tc002VoiceSpeaker(Tc002Speaker& speaker) : speaker_(speaker) {}
  bool available() const override { return speaker_.available(); }
  posix::Hold claimVoiceChannel() override { return speaker_.claimVoiceChannel(); }
  posix::Hold holdMusic() override { return speaker_.holdMusic(); }
  bool quiet() const override { return !speaker_.available() || speaker_.voiceQuiet(); }
  bool play(const std::string& path) override { return speaker_.playVoice(path); }
  void stop() override { speaker_.stopVoice(); }
  bool finished() const override { return !speaker_.available() || speaker_.voiceFinished(); }
  bool heard() const override { return speaker_.voiceHeard(); }

 private:
  Tc002Speaker& speaker_;
};

}
