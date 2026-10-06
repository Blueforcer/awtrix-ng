#pragma once
#include <functional>
#include <string>

#include "platform/tc002/contract/MicrophoneStream.h"
#include "platform/tc002/voice/VoiceConfig.h"

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
    std::function<bool(StreamControl)> microphone;
    std::function<bool(const std::string&)> speak;
    std::function<void()> stopSpeaker;
  };
  explicit AssistSession(Ports ports) : ports_(std::move(ports)) {}
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
  bool ready() const { return state_ == State::Ready && supported_; }
  bool microphoneAvailable() const { return supported_; }
  int captureEpoch() const { return epoch_; }
  // Why the last attempt failed, as a code the web UI translates; empty when none.
  const std::string& error() const { return error_; }
  const std::string& pipelines() const { return pipelines_; }
  static const char* name(State state);

 private:
  bool send(std::string json);
  bool audio(const StreamEvent& event, int64_t now);
  void fail(const char* code, int64_t now);
  void stopCapture(int64_t now);
  void eof(int64_t now);
  Ports ports_;
  Config config_;
  State state_ = State::Offline;
  std::string error_, pipelines_ = "[]";
  int epoch_ = 0, request_ = 0, run_ = 0, list_ = 0, handler_ = -1;
  bool supported_ = false, capture_ = false, stopping_ = false,
       started_ = false;
  bool stt_ = false, recognitionEnded_ = false, ended_ = false, eof_ = false,
       runEnded_ = false, playbackEnded_ = false;
  uint32_t samples_ = 0;
  int64_t deadline_ = 0, keepalive_ = 0, began_ = 0, probeAt_ = 0;
};
}  // namespace awtrix::tc002::voice
