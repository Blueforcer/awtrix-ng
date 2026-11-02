#pragma once

#include <cstddef>
#include <cstdint>
#include <functional>
#include <optional>
#include <string>
#include <vector>

#include "platform/tc002/contract/MicrophoneClient.h"
#include "platform/tc002/runtime/MicrophoneHub.h"
#include "platform/tc002/voice/WavWriter.h"
#include "platform/tc002/wake/Endpointer.h"

namespace awtrix::tc002::voice {

class VoiceCapture {
 public:
  enum class Outcome { Recorded, Silence, NoRoom, Failed };
  struct Result {
    Outcome outcome = Outcome::Failed;
    uint32_t ms = 0;
    std::size_t bytes = 0;
    std::string error;
  };

  VoiceCapture(MicrophonePort microphone, std::function<void(MicrophoneMark)> startFrom, std::string modelPath);
  ~VoiceCapture();
  VoiceCapture(const VoiceCapture&) = delete;
  VoiceCapture& operator=(const VoiceCapture&) = delete;

  bool prepare(std::string& error);
  bool start(const std::string& path, const wake::EndpointSettings& settings, MicrophoneMark from, int64_t now);
  void receive(const StreamEvent& event, int64_t now);
  void tick(int64_t now);
  void stop();
  void cancel();
  bool starting() const {
    return microphone_.phase() == MicrophoneClient::Phase::Opening ||
           microphone_.phase() == MicrophoneClient::Phase::Starting;
  }
  uint8_t level() const { return recording() ? level_ : 0; }
  std::size_t maxBytes(const wake::EndpointSettings& settings) const;
  std::optional<Result> take();

 private:
  bool active() const { return microphone_.phase() != MicrophoneClient::Phase::Idle; }
  bool recording() const { return microphone_.phase() == MicrophoneClient::Phase::Streaming; }
  void record(const std::vector<int16_t>& block);
  void finish(Outcome outcome, std::string error = {});

  MicrophoneClient microphone_;
  std::function<void(MicrophoneMark)> startFrom_;
  const std::string modelPath_;
  std::optional<wake::Endpointer> endpointer_;
  WavWriter wav_;
  std::optional<Result> result_;
  int64_t recordingAt_ = 0;
  int64_t maxMs_ = 0;
  uint8_t level_ = 0;
};

}
