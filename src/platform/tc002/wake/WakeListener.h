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
#include "platform/tc002/wake/WakeSensitivity.h"

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
  static constexpr int64_t kModelOffMs = 60000;

  static std::unique_ptr<WakeListener> open(const std::string& modelPath, MicrophonePort microphone,
                                            std::unique_ptr<posix::Executor> worker, std::string& error);
  ~WakeListener();
  WakeListener(const WakeListener&) = delete;
  WakeListener& operator=(const WakeListener&) = delete;

  std::optional<WakeHeard> tick(int64_t now, Listen listen);
  void setSensitivity(WakeSensitivity level) { sensitivity_ = level; }
  void receive(const StreamEvent& event, int64_t now);
  void cancel();

 private:
  struct Block {
    std::vector<int16_t> samples;
    bool reset = false;
    uint32_t firstSample = 0;
    bool muted = false;
    WakeSensitivity sensitivity = WakeSensitivity::Normal;
  };

  WakeListener(std::string modelPath, std::unique_ptr<WakeDetector> detector, MicrophonePort microphone,
               std::unique_ptr<posix::Executor> worker);
  bool loadDetector(int64_t now);
  void unloadDetector();
  void apply(MicrophoneClient::Report report, int64_t now);
  void unavailable(int64_t now);
  void failed(int64_t now);
  void restart(uint32_t origin);
  void queueAudio(const StreamEvent& event);
  void queue(Block block);
  void drain();

  const std::string modelPath_;
  MicrophoneClient microphone_;
  std::string error_;
  int64_t retryAt_ = 0, wantedAt_ = 0;
  unsigned failures_ = 0;
  bool muted_ = false;
  WakeSensitivity sensitivity_ = WakeSensitivity::Normal;

  std::mutex mutex_;
  std::deque<Block> blocks_;
  std::vector<std::vector<int16_t>> spareSamples_;
  std::vector<WakeHeard> heard_;
  bool draining_ = false;
  std::unique_ptr<WakeDetector> detector_;
  uint32_t detectorOrigin_ = 0;
  std::unique_ptr<posix::Executor> worker_;
};

}
