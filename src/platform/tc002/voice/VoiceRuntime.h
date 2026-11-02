#pragma once
#include <string>
#include <string_view>

#include "platform/linux/net/FileDownload.h"
#include "platform/posix/Hold.h"
#include "platform/tc002/voice/AssistSession.h"
#include "platform/tc002/voice/AssistantRouter.h"
#include "platform/tc002/voice/VoiceOverlay.h"
#include "platform/tc002/voice/VoiceSpeaker.h"
namespace awtrix::tc002::voice {
class VoiceRuntime : public AssistantRouter::Voice {
 public:
  VoiceRuntime(std::string directory, VoiceSpeaker& speaker, MicrophonePort microphone);
  ~VoiceRuntime() override;
  void tick(int64_t now, bool allowed);
  bool begin(int64_t now) override;
  void endInput(int64_t now);
  void receive(const StreamEvent& event, int64_t now);
  void cancel();
  bool eraseConfig() {
    cancel();
    return config_.erase();
  }
  bool off() const override { return config_.get().assistant == kAssistantOff; }
  bool homeAssistant() const override { return config_.get().assistant == kHomeAssistant; }
  const std::string& assistantApp() const override { return config_.assistantApp(); }
  // Off means the knob's long press does nothing: no session is started.
  bool ready() const override {
    return homeAssistant() && !pendingStart_ && !errorFeedback_ && session_.ready();
  }
  bool wakeWord() const override { return config_.get().wakeWord; }
  wake::WakeSensitivity wakeSensitivity() const override { return config_.get().wakeSensitivity; }
  bool busy() const override {
    return pendingStart_ || errorFeedback_ || session_.busy();
  }
  bool starting() const override { return pendingStart_ || session_.starting(); }
  void draw(Canvas& canvas, int64_t now);
  const ConfigStore& config() const { return config_; }
  const AssistSession& session() const { return session_; }
  bool loaded() const { return loaded_; }
  bool updateConfig(std::string_view json, ConfigStore::Rejection& rejection);

 private:
  void connectIfDue(const Config& config, int64_t now);
  void pumpSocket(int64_t now);
  void startPending(int64_t now);
  void pollDownload(int64_t now);
  void finishPlayback(int64_t now);
  void releaseVoiceChannel(int64_t now);
  void removePlaybackFile();
  void stopSpeaker();
  ConfigStore config_;
  VoiceSpeaker& speaker_;
  WebSocket socket_;
  net::FileDownload download_;
  AssistSession session_;
  VoiceOverlay overlay_;
  uint64_t generation_ = 0, downloadGeneration_ = 0;
  int64_t reconnectAt_ = 0;
  int64_t startAt_ = 0;
  int64_t feedbackUntil_ = 0;
  bool errorFeedback_ = false;
  bool loaded_ = false, playing_ = false, pendingStart_ = false;
  std::string playbackPath_;
  posix::Hold voiceChannel_;
};
}  // namespace awtrix::tc002::voice
