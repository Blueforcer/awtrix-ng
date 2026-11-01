#pragma once
#include <functional>
#include <string>
#include <vector>

#include "core/render/Font.h"
#include "platform/linux/net/FileDownload.h"
#include "platform/posix/Hold.h"
#include "platform/tc002/voice/AssistSession.h"
#include "platform/tc002/voice/AssistantRouter.h"
#include "platform/tc002/voice/VoiceOverlay.h"
#include "platform/tc002/voice/VoiceSpeaker.h"
namespace httplib {
struct Request;
struct Response;
}  // namespace httplib
namespace awtrix::tc002::voice {
class VoiceRuntime : public AssistantRouter::Voice {
 public:
  struct Hooks {
    std::function<std::vector<std::string>()> assistantApps;
    std::function<bool()> startAssistant;
  };
  VoiceRuntime(std::string directory, VoiceSpeaker& speaker, MicrophonePort microphone,
               bool wakeWordAvailable, Hooks hooks);
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
  bool homeAssistant() const override { return config_.get().assistant == kHomeAssistant; }
  const std::string& assistantApp() const override { return config_.assistantApp(); }
  // Off means the knob's long press does nothing: no session is started.
  bool ready() const override {
    return config_.get().enabled && !pendingStart_ && !errorFeedback_ && session_.ready();
  }
  bool wakeWord() const override { return config_.get().wakeWord; }
  bool busy() const override {
    return pendingStart_ || errorFeedback_ || session_.busy();
  }
  bool starting() const override { return pendingStart_ || session_.starting(); }
  bool draw(Canvas& canvas, const GfxFont& font, int64_t now);
  bool handle(const httplib::Request& request, httplib::Response& response);

 private:
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
  const bool wakeWordAvailable_;
  const Hooks hooks_;
  std::string playbackPath_;
  posix::Hold voiceChannel_;
};
}  // namespace awtrix::tc002::voice
