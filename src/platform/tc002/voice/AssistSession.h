#pragma once
#include <functional>
#include <string>

#include "platform/tc002/contract/MicrophoneClient.h"
#include "platform/tc002/voice/VoiceConfig.h"

namespace awtrix::api {
class JsonReader;
}

namespace awtrix::tc002::voice {
// Render-thread state machine. Transport and microphone ownership are injected;
// no socket, decoder, filesystem or blocking operation belongs in this class.
class AssistSession {
 public:
  enum class State {
    Offline,
    Connecting,
    Ready,
    Starting,
    Listening,
    Processing,
    Speaking,
    Error
  };
  struct Ports {
    std::function<bool(std::string, bool)> send;
    std::function<void()> disconnect;
    MicrophonePort microphone;
    std::function<bool(const std::string&)> speak;
    std::function<void()> stopSpeaker;
  };
  explicit AssistSession(Ports ports) : ports_(std::move(ports)), microphone_(std::move(ports_.microphone)) {}
  void connecting(const Config& config, int64_t now);
  void text(std::string_view json, int64_t now);
  void lost(int64_t now);
  bool begin(int64_t now);
  void endInput(int64_t now);
  void cancel();
  void receive(const StreamEvent& event, int64_t now);
  void tick(int64_t now);
  void playbackDone(bool success, int64_t now);
  State state() const { return state_; }
  bool busy() const;
  bool starting() const { return state_ == State::Starting; }
  bool ready() const { return state_ == State::Ready && supported_; }
  bool microphoneAvailable() const { return supported_; }
  int captureEpoch() const { return microphone_.epoch(); }
  const std::string& error() const { return error_; }
  const std::string& lastError() const { return lastError_; }
  const std::string& pipelines() const { return pipelines_; }
  static const char* name(State state);

 private:
  static constexpr int64_t kLastErrorMs = 300000;
  static constexpr int64_t kConnectMs = 10000, kStartMs = 5000, kRetryMs = 5000, kCaptureStartMs = 1500,
                           kCaptureEndMs = 1500, kListenMs = 60000, kAnswerMs = 30000, kSpeakMs = 60000;
  bool send(std::string json);
  bool authenticate(const std::string& type, int64_t now);
  void pipelinesListed(const api::JsonReader& reader, int64_t now);
  void runEvent(const api::JsonReader& reader, int64_t now);
  void becomeReady();
  void drop();
  bool audio(const StreamEvent& event, int64_t now);
  void apply(MicrophoneClient::Report report, int64_t now);
  void fail(const char* code, int64_t now);
  void stopCapture(int64_t now);
  void eof(int64_t now);
  Ports ports_;
  MicrophoneClient microphone_;
  Config config_;
  State state_ = State::Offline;
  std::string error_, lastError_, pipelines_ = "[]";
  int request_ = 0, run_ = 0, list_ = 0, handler_ = -1;
  bool supported_ = false;
  bool stt_ = false, recognitionEnded_ = false, runEnded_ = false, playbackEnded_ = false;
  int64_t deadline_ = 0, began_ = 0, probeAt_ = 0, lastErrorUntil_ = 0;
};
}  // namespace awtrix::tc002::voice
