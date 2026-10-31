#pragma once
#include "core/render/Font.h"
#include "platform/tc002/runtime/Tc002Speaker.h"
#include "platform/tc002/voice/AssistSession.h"
#include "platform/linux/net/FileDownload.h"
#include "platform/tc002/voice/VoiceOverlay.h"
namespace httplib {
struct Request;
struct Response;
}  // namespace httplib
namespace awtrix::tc002::voice {
class VoiceRuntime {
 public:
  // wakeWordAvailable: the runtime has a wake word to start Voice with.
  VoiceRuntime(std::string directory, Tc002Speaker& speaker, MicrophonePort microphone,
               bool wakeWordAvailable);
  ~VoiceRuntime();
  void tick(int64_t now, bool allowed);
  bool begin(int64_t now);
  void endInput(int64_t now);
  void receive(const StreamEvent& event, int64_t now);
  void cancel();
  bool eraseConfig() {
    cancel();
    return config_.erase();
  }
  // Off means the knob's long press does nothing: no session is started.
  bool ready() const {
    return config_.get().enabled && !pendingStart_ && !errorFeedback_ && session_.ready();
  }
  bool wakeWord() const { return config_.get().wakeWord; }
  bool busy() const {
    return pendingStart_ || errorFeedback_ || session_.busy();
  }
  // From begin() until the capture runs or the request has ended.
  bool starting() const {
    return pendingStart_ || session_.state() == AssistSession::State::Starting;
  }
  bool draw(Canvas& canvas, const GfxFont& font, int64_t now);
  bool handle(const httplib::Request& request, httplib::Response& response);

 private:
  void stopSpeaker();
  ConfigStore config_;
  Tc002Speaker& speaker_;
  WebSocket socket_;
  net::FileDownload download_;
  AssistSession session_;
  VoiceOverlay overlay_;
  uint64_t generation_ = 0, downloadGeneration_ = 0;
  int64_t reconnectAt_ = 0;
  int64_t startAt_ = 0;
  int64_t feedbackUntil_ = 0;
  bool errorFeedback_ = false;
  bool loaded_ = false, playing_ = false, owned_ = false, pendingStart_ = false;
  const bool wakeWordAvailable_;
  std::string playbackPath_;
};
}  // namespace awtrix::tc002::voice
