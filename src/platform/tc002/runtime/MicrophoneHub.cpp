#include "platform/tc002/runtime/MicrophoneHub.h"

#include <algorithm>
#include <climits>

namespace awtrix::tc002 {
namespace {
constexpr const char* kRefused = "supervisor link refused";
}  // namespace

int MicrophoneHub::attach(Deliver deliver) {
  const int id = nextId_;
  nextId_ = nextId_ == INT_MAX ? 1 : nextId_ + 1;
  consumers_[id].deliver = std::move(deliver);
  return id;
}

void MicrophoneHub::detach(int consumer) {
  consumers_.erase(consumer);
  releaseIfUnused();
}

bool MicrophoneHub::control(int id, const StreamControl& control) {
  auto it = consumers_.find(id);
  if (it == consumers_.end()) return false;
  Consumer& consumer = it->second;
  switch (control.operation) {
    case StreamControl::Probe:
      // Once the daemon has answered for the current stream, the hub answers alone, as the daemon would.
      if (state_ != State::Idle && state_ != State::Probing) {
        support(id, control.epoch, true);
      } else {
        consumer.probe = control.epoch;
        if (state_ == State::Idle) openUpstream(false);
      }
      break;
    case StreamControl::Start:
      if (consumer.wanted && consumer.epoch == control.epoch) break;
      if (consumer.wanted) finish(id, consumer, {});
      consumer.epoch = control.epoch;
      consumer.wanted = true;
      consumer.renewedAt = now_;
      lingerUntil_ = -1;
      if (state_ == State::Streaming) begin(id, consumer);
      else if (state_ == State::Idle) openUpstream(false);
      break;
    case StreamControl::Keepalive:
      if (consumer.wanted && consumer.epoch == control.epoch) consumer.renewedAt = now_;
      break;
    case StreamControl::Stop:
      if (consumer.probe == control.epoch) consumer.probe = 0;
      if (consumer.wanted && consumer.epoch == control.epoch) {
        finish(id, consumer, {});
        releaseIfUnused();
      }
      break;
  }
  return true;
}

MicrophonePort MicrophoneHub::port(int consumer) {
  return [this, consumer](StreamControl request) { return control(consumer, request); };
}

void MicrophoneHub::receive(const StreamEvent& e, int64_t now) {
  now_ = now;
  if (!epoch_ || e.epoch != epoch_) return;
  switch (e.kind) {
    case StreamEvent::Kind::Support:
      if (state_ != State::Probing) break;
      // A consumer may start or probe again from inside its answer, as the wake word listener starts there.
      // A start joins this stream; a probe gets the same answer.
      answerProbes(e.available);
      flush();
      answerProbes(e.available);
      if (!e.available) {
        endUpstream("microphone stream unavailable");
      } else if (anyWanted()) {
        startUpstream();
      } else {
        state_ = State::Idle;
      }
      break;
    case StreamEvent::Kind::Started:
      if (state_ != State::Starting) break;
      state_ = State::Streaming;
      next_ = 0;
      ring_.assign(kRingSamples, 0);
      ringHead_ = 0;
      ringCount_ = 0;
      for (auto& [id, consumer] : consumers_)
        if (consumer.wanted) begin(id, consumer);
      break;
    case StreamEvent::Kind::Audio:
      if (state_ != State::Streaming) break;
      if (tap_) tap_(e, now);
      // The held audio must be one run: a gap starts it over.
      if (e.firstSample != next_) ringCount_ = 0;
      for (const int16_t sample : e.samples) {
        ring_[ringHead_] = sample;
        ringHead_ = (ringHead_ + 1) % kRingSamples;
      }
      ringCount_ = static_cast<uint32_t>(
          std::min<std::size_t>(std::size_t{ringCount_} + e.samples.size(), kRingSamples));
      // Unsigned arithmetic carries a gap and the wrap past 2^32 into each consumer's count.
      for (const auto& [id, consumer] : consumers_) {
        if (!consumer.started) continue;
        StreamEvent audio = e;
        audio.firstSample = e.firstSample - consumer.first;
        answer(id, consumer.epoch, std::move(audio));
      }
      next_ = e.firstSample + static_cast<uint32_t>(e.samples.size());
      break;
    case StreamEvent::Kind::Ended:
      endUpstream(e.error);
      break;
  }
  flush();
}

void MicrophoneHub::tick(int64_t now) {
  now_ = now;
  for (auto& [id, consumer] : consumers_)
    if (consumer.wanted && now - consumer.renewedAt > kStreamLeaseMs) finish(id, consumer, "consumer lease expired");
  releaseIfUnused();
  switch (state_) {
    case State::Idle:
      break;
    case State::Probing:
      if (now - stateAt_ >= kAnswerMs) endUpstream("microphone did not answer");
      break;
    case State::Starting:
      if (now - stateAt_ >= kAnswerMs) {
        up(StreamControl::Stop);
        endUpstream("microphone did not answer");
        break;
      }
      [[fallthrough]];
    case State::Streaming:
      if (lingerUntil_ >= 0 && now >= lingerUntil_) {
        lingerUntil_ = -1;
        stopUpstream();
      } else if (now - keepaliveAt_ >= kStreamKeepaliveMs) {
        keepaliveAt_ = now;
        up(StreamControl::Keepalive);
      }
      break;
    case State::Stopping:
      // The daemon's consumer lease ends the stream on its own if the end never arrives.
      if (now - stateAt_ >= kAnswerMs) endUpstream({});
      break;
  }
  flush();
}

void MicrophoneHub::hold(bool held) {
  if (held == held_) return;
  held_ = held;
  if (held) lingerUntil_ = -1;
  else releaseIfUnused();
}

void MicrophoneHub::close() {
  held_ = false;
  lingerUntil_ = -1;
  for (auto& [id, consumer] : consumers_) {
    consumer.probe = 0;
    if (consumer.wanted) finish(id, consumer, "microphone closed");
  }
  if (state_ == State::Starting || state_ == State::Streaming) stopUpstream();
}

MicrophoneMark MicrophoneHub::markOf(int id, uint32_t sample) const {
  const auto it = consumers_.find(id);
  if (state_ != State::Streaming || it == consumers_.end() || !it->second.started) return {};
  return {epoch_, it->second.first + sample};
}

void MicrophoneHub::startFrom(int id, MicrophoneMark mark) {
  const auto it = consumers_.find(id);
  if (it != consumers_.end()) it->second.from = mark;
}

bool MicrophoneHub::up(StreamControl::Operation operation) {
  return send_ && send_(encodeStreamControl({epoch_, operation}));
}

// A new attempt under a new epoch. It probes first, unless the stream that just ended showed the
// microphone is there.
void MicrophoneHub::openUpstream(bool available) {
  epoch_ = epoch_ == INT_MAX ? 1 : epoch_ + 1;
  if (available) {
    startUpstream();
    return;
  }
  state_ = State::Probing;
  stateAt_ = now_;
  if (!up(StreamControl::Probe)) endUpstream(kRefused);
}

void MicrophoneHub::startUpstream() {
  state_ = State::Starting;
  stateAt_ = keepaliveAt_ = now_;
  if (!up(StreamControl::Start)) endUpstream(kRefused);
}

void MicrophoneHub::stopUpstream() {
  up(StreamControl::Stop);
  state_ = State::Stopping;
  stateAt_ = now_;
}

// Called whenever a consumer or the hold may have let go: a streaming upstream lingers, a starting
// one stops.
void MicrophoneHub::releaseIfUnused() {
  if (held_ || anyWanted()) return;
  if (state_ == State::Streaming && lingerUntil_ < 0) lingerUntil_ = now_ + kLingerMs;
  else if (state_ == State::Starting) stopUpstream();
}

void MicrophoneHub::endUpstream(const std::string& error) {
  const bool asked = state_ == State::Stopping;
  state_ = State::Idle;
  lingerUntil_ = -1;
  std::vector<int16_t>().swap(ring_);
  ringCount_ = 0;
  // The hub stops only a stream nobody wants, so a consumer that wants one now started during the stop.
  if (asked) {
    if (anyWanted()) openUpstream(true);
    return;
  }
  // The hub drops the probes that waited for this attempt; each consumer's own probe timeout applies.
  for (auto& [id, consumer] : consumers_) {
    consumer.probe = 0;
    if (consumer.wanted) finish(id, consumer, error);
  }
}

void MicrophoneHub::answerProbes(bool available) {
  for (auto& [id, consumer] : consumers_) {
    if (!consumer.probe) continue;
    support(id, consumer.probe, available);
    consumer.probe = 0;
  }
}

void MicrophoneHub::begin(int id, Consumer& consumer) {
  // How far back the consumer starts: from its mark when this stream holds it, at most the held
  // second; a mark ahead of the stream (a wrapped difference) starts it now.
  uint32_t back = 0;
  if (consumer.from.epoch == epoch_) {
    const uint32_t behind = next_ - consumer.from.sample;
    if (behind < 0x80000000u) back = std::min(behind, ringCount_);
  }
  consumer.from = {};
  consumer.started = true;
  consumer.first = next_ - back;
  consumer.renewedAt = now_;
  StreamEvent started;
  started.kind = StreamEvent::Kind::Started;
  answer(id, consumer.epoch, std::move(started));
  for (uint32_t done = 0; done < back;) {
    const uint32_t count = std::min<uint32_t>(back - done, kStreamBlockSamples);
    StreamEvent held;
    held.kind = StreamEvent::Kind::Audio;
    held.firstSample = done;
    held.samples.resize(count);
    const std::size_t at = (ringHead_ + kRingSamples - back + done) % kRingSamples;
    for (uint32_t i = 0; i < count; ++i) held.samples[i] = ring_[(at + i) % kRingSamples];
    answer(id, consumer.epoch, std::move(held));
    done += count;
  }
}

void MicrophoneHub::finish(int id, Consumer& consumer, std::string error) {
  StreamEvent ended;
  ended.kind = StreamEvent::Kind::Ended;
  // Like the daemon's, the end of a started stream names the sample after the consumer's last one.
  if (consumer.started) ended.firstSample = next_ - consumer.first;
  ended.error = std::move(error);
  consumer.wanted = consumer.started = false;
  answer(id, consumer.epoch, std::move(ended));
}

void MicrophoneHub::support(int id, int epoch, bool available) {
  StreamEvent event;
  event.kind = StreamEvent::Kind::Support;
  event.available = available;
  answer(id, epoch, std::move(event));
}

void MicrophoneHub::answer(int id, int epoch, StreamEvent event) {
  event.epoch = epoch;
  outbox_.emplace_back(id, std::move(event));
}

// Only receive() and tick() deliver, and only what was queued before: a consumer may call back in, and the
// answer to that call waits for a later flush, as the daemon's would. Both queues keep their memory.
void MicrophoneHub::flush() {
  delivering_.swap(outbox_);
  for (auto& [id, event] : delivering_) {
    auto it = consumers_.find(id);
    if (it == consumers_.end() || !it->second.deliver) continue;
    const Deliver deliver = it->second.deliver;
    deliver(event, now_);
  }
  delivering_.clear();
}

bool MicrophoneHub::anyWanted() const {
  for (const auto& entry : consumers_)
    if (entry.second.wanted) return true;
  return false;
}

}  // namespace awtrix::tc002
