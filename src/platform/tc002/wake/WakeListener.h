#pragma once

#include <condition_variable>
#include <cstdint>
#include <deque>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <thread>
#include <vector>

#include "platform/tc002/contract/MicrophoneStream.h"

namespace awtrix::tc002::wake {

class WakeDetector;

// A detection: the window mean and the end of the frames that fired, as a position in the listener's
// own stream (the hub's numbering for this consumer).
struct WakeHeard {
  float score = 0.0f;
  uint32_t sample = 0;
};

// Listens for the wake word on the continuous microphone stream while the caller wants it, and
// reports each detection once. The microphone hub shares the stream with Voice; the caller stops
// listening whenever Voice is busy. Detection runs on a worker thread; the render thread only queues
// blocks and collects results.
class WakeListener {
 public:
  // The model's .tflite file; its probability_cutoff and sliding_window_size come from the .json file
  // of the same name beside it. nullptr and a reason when either cannot be used.
  static std::unique_ptr<WakeListener> open(const std::string& modelPath, MicrophonePort microphone,
                                            std::string& error);
  ~WakeListener();
  WakeListener(const WakeListener&) = delete;
  WakeListener& operator=(const WakeListener&) = delete;

  // listen is false while Voice is off, not connected or busy. A detection once, while listening.
  std::optional<WakeHeard> tick(int64_t now, bool listen);
  void receive(const StreamEvent& event, int64_t now);
  void cancel();

 private:
  enum class State { Idle, Unavailable, Probing, Starting, Listening, Stopping };
  struct Block {
    std::vector<int16_t> samples;
    bool reset = false;
    uint32_t at = 0;  // the stream position of its first sample
  };

  WakeListener(std::unique_ptr<WakeDetector> detector, MicrophonePort microphone);
  void send(StreamControl::Operation operation);
  void startStream(int64_t now);
  void stopStream(int64_t now);
  void ended(const std::string& error, int64_t now);
  void queue(Block block);
  void run();

  MicrophonePort microphone_;
  State state_ = State::Idle;
  std::string error_;
  int epoch_ = 0, nextEpoch_ = 1;
  int64_t keepaliveAt_ = 0, retryAt_ = 0, stopDeadline_ = -1, stateAt_ = 0;
  uint32_t expected_ = 0;
  unsigned failures_ = 0;

  // Shared with the worker thread, which alone runs the detector.
  std::mutex mutex_;
  std::condition_variable wake_;
  std::deque<Block> blocks_;
  std::vector<WakeHeard> heard_;
  uint32_t base_ = 0;  // the stream position where the detector's count began
  bool quit_ = false;
  std::unique_ptr<WakeDetector> detector_;
  std::thread worker_;
};

}  // namespace awtrix::tc002::wake
