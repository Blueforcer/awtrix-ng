#pragma once

#include <cstdint>
#include <functional>
#include <map>
#include <string>
#include <utility>
#include <vector>

#include "platform/tc002/contract/MicrophoneStream.h"
#include "platform/tc002/contract/SupervisorProtocol.h"

namespace awtrix::tc002 {

constexpr uint32_t kDmaHalfSamples = 24;

constexpr uint32_t roundUpToDmaHalf(uint32_t samples) {
  return (samples + kDmaHalfSamples - 1) / kDmaHalfSamples * kDmaHalfSamples;
}

struct MicrophoneMark {
  int epoch = 0;
  uint32_t sample = 0;
};

class MicrophoneHub {
 public:
  using Send = std::function<bool(std::string)>;
  using Deliver = std::function<void(const StreamEvent&, int64_t)>;
  static constexpr int64_t kLingerMs = 1000;
  static constexpr uint32_t kRingSamples = roundUpToDmaHalf(kMicrophonePcmRate);

  class Consumer {
   public:
    Consumer() = default;
    Consumer(Consumer&& other) noexcept
        : hub_(std::exchange(other.hub_, nullptr)), id_(std::exchange(other.id_, 0)) {}
    Consumer& operator=(Consumer&& other) noexcept {
      if (this != &other) {
        detach();
        hub_ = std::exchange(other.hub_, nullptr);
        id_ = std::exchange(other.id_, 0);
      }
      return *this;
    }
    Consumer(const Consumer&) = delete;
    Consumer& operator=(const Consumer&) = delete;
    ~Consumer() { detach(); }

    MicrophonePort port() const;
    void startFrom(MicrophoneMark mark) const {
      if (hub_) hub_->startFrom(id_, mark);
    }
    MicrophoneMark markOf(uint32_t sample) const { return hub_ ? hub_->markOf(id_, sample) : MicrophoneMark{}; }
    void detach();

   private:
    friend class MicrophoneHub;
    Consumer(MicrophoneHub& hub, int id) : hub_(&hub), id_(id) {}

    MicrophoneHub* hub_ = nullptr;
    int id_ = 0;
  };

  MicrophoneHub(Send send, Deliver tap) : send_(std::move(send)), tap_(std::move(tap)) {}
  MicrophoneHub(const MicrophoneHub&) = delete;
  MicrophoneHub& operator=(const MicrophoneHub&) = delete;

  Consumer attach(Deliver deliver);

  void receive(const StreamEvent& upstream, int64_t now);
  void tick(int64_t now);
  void hold(bool held);
  void close();
  MicrophoneMark mark() const { return {state_ == State::Streaming ? epoch_ : 0, nextUpstreamSample_}; }
  bool live(MicrophoneMark mark) const { return state_ == State::Streaming && mark.epoch == epoch_; }

  bool active() const { return state_ != State::Idle && (state_ != State::Probing || anyWanted()); }

 private:
  enum class State { Idle, Probing, Starting, Streaming, Stopping };
  struct ConsumerState {
    Deliver deliver;
    int probeEpoch = 0;
    int epoch = 0;
    bool wanted = false, started = false;
    uint32_t firstUpstreamSample = 0;
    int64_t renewedAt = 0;
    MicrophoneMark startMark;
  };

  void detach(int id);
  bool control(int id, const StreamControl& control);
  MicrophoneMark markOf(int id, uint32_t sample) const;
  void startFrom(int id, MicrophoneMark mark);
  bool up(StreamControl::Operation operation);
  void openUpstream(bool knownAvailable);
  void startUpstream();
  void stopUpstream();
  void releaseIfUnused();
  void endUpstream(const std::string& error);
  void answerProbes(bool available);
  void begin(int id, ConsumerState& consumer);
  void finish(int id, ConsumerState& consumer, std::string error);
  void support(int id, int epoch, bool available);
  void answer(int id, int epoch, StreamEvent event);
  void flush();
  bool anyWanted() const;

  Send send_;
  Deliver tap_;
  std::map<int, ConsumerState> consumers_;
  std::vector<std::pair<int, StreamEvent>> outbox_, delivering_;
  State state_ = State::Idle;
  bool held_ = false;
  uint32_t nextUpstreamSample_ = 0;
  std::vector<int16_t> ring_;
  std::size_t ringHead_ = 0;
  uint32_t ringCount_ = 0;
  int epoch_ = 0, nextId_ = 1;
  int64_t now_ = 0, stateAt_ = 0, keepaliveAt_ = 0, lingerUntil_ = -1;
};

}
