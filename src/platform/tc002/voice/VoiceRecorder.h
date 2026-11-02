#pragma once

#include <cstdint>
#include <functional>
#include <optional>
#include <string>

#include "platform/tc002/runtime/MicrophoneHub.h"
#include "platform/tc002/voice/ClipStore.h"
#include "platform/tc002/voice/VoiceCapture.h"
#include "platform/tc002/voice/VoiceEvent.h"
#include "platform/tc002/voice/VoiceSpeaker.h"

namespace awtrix::tc002::voice {

class VoiceRecorder {
 public:
  struct Services {
    MicrophonePort microphone;
    std::function<void(MicrophoneMark)> startFrom;
    std::function<bool(MicrophoneMark)> live;
    VoiceSpeaker* speaker = nullptr;
    std::function<bool()> assistantBusy;
    std::function<bool(const std::string&)> visible;
    std::function<int64_t()> clock;
    std::string voiceActivityModel;
    std::function<bool()> claimVoiceChannel;
    std::function<MicrophoneMark(const std::string&)> takeMark;
    std::function<void(VoiceEvent)> emit;
  };

  VoiceRecorder(ClipStore& clips, Services services);
  VoiceRecorder(const VoiceRecorder&) = delete;
  VoiceRecorder& operator=(const VoiceRecorder&) = delete;

  void receive(const StreamEvent& event, int64_t now) { capture_.receive(event, now); }
  void update(int64_t now);
  bool starting() const { return (recording_ && recording_->pending) || capture_.starting(); }
  bool active() const { return recording_ || playback_; }
  bool holds(const std::string& app) const { return records(app) || plays(app); }

  int record(const std::string& app, const wake::EndpointSettings& settings, bool fromTrigger);
  void stop(const std::string& app);
  void end(const std::string& app);
  int play(const std::string& app, ClipId clip);
  const char* state(const std::string& app) const;
  uint8_t level(const std::string& app) const { return records(app) ? capture_.level() : 0; }

 private:
  struct PendingCapture {
    wake::EndpointSettings settings;
    int64_t since = 0;
  };
  struct Recording {
    std::string app;
    ClipId clip = 0;
    int number = 0;
    std::optional<PendingCapture> pending;
  };
  struct Playback {
    std::string app;
    int number = 0;
  };
  bool records(const std::string& app) const { return recording_ && recording_->app == app; }
  bool plays(const std::string& app) const { return playback_ && playback_->app == app; }
  void startCapture(const wake::EndpointSettings& settings, MicrophoneMark from, int64_t now);
  void deliverRecording(uint32_t ms);
  void endRecording(int status);
  void clearRecording();
  void stopPlayback();
  void endPlayback(bool heard);

  ClipStore& clips_;
  Services services_;
  VoiceCapture capture_;
  std::optional<Recording> recording_;
  std::optional<Playback> playback_;
  int recordNumber_ = 0, playNumber_ = 0;
};

}
