#pragma once

#include <cstddef>
#include <cstdint>
#include <deque>
#include <string>
#include <vector>

#include "platform/tc002/contract/AudioProtocol.h"

namespace awtrix {
namespace tc002 {

// The runtime end of the awtrix-tc002-audio-pcm socket. Never blocks: PCM is only sent within the
// credit the helper grants, control messages queue in order and go out ahead of any later PCM.
class Tc002AudioLink {
 public:
  explicit Tc002AudioLink(int fd);
  ~Tc002AudioLink();
  Tc002AudioLink(const Tc002AudioLink&) = delete;
  Tc002AudioLink& operator=(const Tc002AudioLink&) = delete;

  int fd() const { return fd_; }
  void poll();
  bool wantsWrite() const { return !control_.empty(); }
  bool ready() const { return hello_ && !failed_; }
  bool failed() const { return failed_; }
  const std::string& failure() const { return failure_; }

  uint32_t open(uint32_t rate, uint8_t channels);
  std::size_t credit() const;
  bool sendPcm(const int16_t* samples, std::size_t count);
  void drain();
  void stop();
  void setVolume(uint8_t percent);

  uint32_t generation() const { return generation_; }
  bool finished(uint32_t generation) const;
  uint32_t queuedMs() const;
  const tc002_audio_status& status() const { return status_; }

 private:
  void queue(const uint8_t* message, std::size_t size);
  void flushControl();
  void receive(const uint8_t* data, std::size_t size);
  void fail(const std::string& why);
  void failSocket();

  int fd_;
  bool hello_ = false;
  bool failed_ = false;
  std::string failure_;
  tc002_audio_hello info_{};
  tc002_audio_status status_{};
  uint32_t generation_ = 0;
  bool open_ = false;
  bool draining_ = false;
  uint32_t rate_ = 0;
  uint8_t channels_ = 0;
  uint32_t sent_ = 0;
  uint32_t consumed_ = 0;
  std::deque<std::vector<uint8_t>> control_;
};

}
}
