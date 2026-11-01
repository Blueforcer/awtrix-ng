#pragma once

#include <cstdint>
#include <deque>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <vector>

#include "platform/posix/Executor.h"
#include "platform/tc002/contract/MicrophoneClient.h"

namespace awtrix::tc002::wake {

class WakeDetector;

struct WakeHeard {
  float score = 0.0f;
  uint32_t sample = 0;
};

class WakeListener {
 public:
  enum class Listen { Off, Muted, On };
  static constexpr std::size_t kQueueBlocks = 16;

  static std::unique_ptr<WakeListener> open(const std::string& modelPath, MicrophonePort microphone,
                                            std::unique_ptr<posix::Executor> worker, std::string& error);
  ~WakeListener();
  WakeListener(const WakeListener&) = delete;
  WakeListener& operator=(const WakeListener&) = delete;

  std::optional<WakeHeard> tick(int64_t now, Listen listen);
  void receive(const StreamEvent& event, int64_t now);
  void cancel();

 private:
  struct Block {
    std::vector<int16_t> samples;
    bool reset = false;
    uint32_t firstSample = 0;
    bool muted = false;
  };

  WakeListener(std::unique_ptr<WakeDetector> detector, MicrophonePort microphone,
               std::unique_ptr<posix::Executor> worker);
  void apply(MicrophoneClient::Report report, int64_t now);
  void unavailable(int64_t now);
  void failed(int64_t now);
  void restart(uint32_t origin);
  void queue(Block block);
  void drain();

  MicrophoneClient microphone_;
  std::string error_;
  int64_t retryAt_ = 0;
  unsigned failures_ = 0;
  bool muted_ = false;

  std::mutex mutex_;
  std::deque<Block> blocks_;
  std::vector<WakeHeard> heard_;
  bool draining_ = false;
  std::unique_ptr<WakeDetector> detector_;
  uint32_t detectorOrigin_ = 0;
  std::unique_ptr<posix::Executor> worker_;
};

}
