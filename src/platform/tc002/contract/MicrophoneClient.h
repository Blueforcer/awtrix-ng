#pragma once

#include <cstdint>
#include <string>

#include "platform/tc002/contract/MicrophoneStream.h"

namespace awtrix::tc002 {

class MicrophoneClient {
 public:
  enum class Phase { Idle, Probing, Opening, Starting, Streaming, Stopping };
  enum class Report { None, Available, Unavailable, Started, Audio, Gap, Ended, Stopped };

  explicit MicrophoneClient(MicrophonePort port);
  MicrophoneClient(const MicrophoneClient&) = delete;
  MicrophoneClient& operator=(const MicrophoneClient&) = delete;

  void open(int64_t now);
  void probe();
  void probeAgain();
  void start(int64_t now);
  void stop(int64_t now);
  void cancel();
  Report receive(const StreamEvent& event, int64_t now);
  Report tick(int64_t now);

  Phase phase() const { return phase_; }
  bool capturing() const { return phase_ == Phase::Starting || phase_ == Phase::Streaming; }
  bool started() const { return started_; }
  int epoch() const { return epoch_; }
  const std::string& error() const { return error_; }

 private:
  void send(StreamControl::Operation operation);
  void enter(Phase phase, int64_t now);
  void renew();
  void requestStart(int64_t now);

  MicrophonePort port_;
  Phase phase_ = Phase::Idle;
  int epoch_ = 0;
  bool started_ = false;
  uint32_t expected_ = 0;
  int64_t phaseAt_ = 0, keepaliveAt_ = 0;
  std::string error_;
};

}
