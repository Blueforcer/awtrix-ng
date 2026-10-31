#pragma once

#include <cstdint>
#include <functional>
#include <map>
#include <string>
#include <utility>
#include <vector>

#include "platform/tc002/contract/MicrophoneStream.h"

namespace awtrix::tc002 {

// A point in an upstream stream, valid while that stream runs; epoch 0 is no point.
struct MicrophoneMark {
  int epoch = 0;
  uint32_t sample = 0;
};

// Shares the daemon's one microphone stream among several consumers. Each consumer speaks the daemon's
// protocol, under its own epochs and its own sample count. One upstream stream serves them all; once it
// runs, it stays open for a second after the last consumer stops, or after the owner lets go of a hold.
// control() takes the time of the latest tick() or receive(): the owner ticks the hub every frame, also
// while idle, before its consumers. While a stream runs, the hub holds its last second, so a consumer may
// start from a mark in the past.
class MicrophoneHub {
 public:
  using Send = std::function<bool(std::string)>;
  using Deliver = std::function<void(const StreamEvent&, int64_t)>;
  // The upstream keepalive and the consumers' lease are the contract's kStreamKeepaliveMs and
  // kStreamLeaseMs.
  static constexpr int64_t kAnswerMs = 2000, kLingerMs = 1000;
  // Held while an upstream stream runs, for a consumer that starts from a mark in the past.
  static constexpr uint32_t kRingSamples = 16000;

  // send carries encoded StreamControl messages to the daemon; tap sees every upstream audio block.
  MicrophoneHub(Send send, Deliver tap) : send_(std::move(send)), tap_(std::move(tap)) {}

  // A consumer handle, never 0. Its deliver may call control() and detach() again.
  int attach(Deliver deliver);
  // Forgets the consumer without an answer.
  void detach(int consumer);
  // Probe, Start, Keepalive and Stop as the daemon takes them; false only for an unknown consumer. The
  // answers arrive through deliver from a later receive() or tick(), never inside this call.
  bool control(int consumer, const StreamControl& control);
  // The consumer's control() as a port, for a consumer that takes one.
  MicrophonePort port(int consumer);

  void receive(const StreamEvent& upstream, int64_t now);
  void tick(int64_t now);
  // While held, a starting or running upstream stream stays open without a consumer, for one that is
  // about to start; it lingers from the release. A hold opens no stream.
  void hold(bool held);
  // Stops the upstream stream at once, without lingering, as the owner shuts down. Every consumer's
  // stream ends; like any answer, its end reaches the consumer only on a later receive() or tick(). A
  // later start opens a new stream.
  void close();
  // The sample after the latest upstream block, while a stream runs.
  MicrophoneMark mark() const { return {state_ == State::Streaming ? epoch_ : 0, next_}; }
  // A started consumer's own sample position as a mark.
  MicrophoneMark markOf(int consumer, uint32_t sample) const;
  // The consumer's next start begins at the mark when this stream still holds it, else at its oldest
  // held sample; a mark of another stream starts it now. Held audio arrives with hostAtMs 0.
  void startFrom(int consumer, MicrophoneMark mark);

  // From a consumer's start until the upstream stream has ended; no PCM snapshot may be asked for
  // meanwhile. A probe alone does not count.
  bool active() const { return state_ != State::Idle && (state_ != State::Probing || anyWanted()); }

 private:
  enum class State { Idle, Probing, Starting, Streaming, Stopping };
  struct Consumer {
    Deliver deliver;
    int probe = 0;  // the epoch of a probe waiting for the upstream answer, 0 for none
    int epoch = 0;  // the epoch of the consumer's stream
    bool wanted = false, started = false;
    uint32_t first = 0;  // the upstream sample that is the consumer's sample 0
    int64_t renewedAt = 0;
    MicrophoneMark from;  // where the next start begins, epoch 0 for now
  };

  bool up(StreamControl::Operation operation);
  void openUpstream(bool available);
  void startUpstream();
  void stopUpstream();
  void releaseIfUnused();
  void endUpstream(const std::string& error);
  void answerProbes(bool available);
  void begin(int id, Consumer& consumer);
  void finish(int id, Consumer& consumer, std::string error);
  void support(int id, int epoch, bool available);
  void answer(int id, int epoch, StreamEvent event);
  void flush();
  bool anyWanted() const;

  Send send_;
  Deliver tap_;
  std::map<int, Consumer> consumers_;
  std::vector<std::pair<int, StreamEvent>> outbox_, delivering_;
  State state_ = State::Idle;
  bool held_ = false;
  uint32_t next_ = 0;  // the upstream sample after the last block, where a joining consumer starts
  std::vector<int16_t> ring_;  // the latest samples while streaming, ringHead_ the next to write
  std::size_t ringHead_ = 0;
  uint32_t ringCount_ = 0;
  int epoch_ = 0, nextId_ = 1;
  int64_t now_ = 0, stateAt_ = 0, keepaliveAt_ = 0, lingerUntil_ = -1;
};

}  // namespace awtrix::tc002
