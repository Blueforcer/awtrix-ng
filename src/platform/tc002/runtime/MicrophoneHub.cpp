#include "platform/tc002/runtime/MicrophoneHub.h"

#include <algorithm>
#include <utility>

#include "platform/posix/Sequence.h"

namespace awtrix::tc002 {
namespace {
constexpr const char* kRefused = "supervisor link refused";
constexpr const char* kStalled = "microphone stream stalled";
}

MicrophonePort MicrophoneHub::Consumer::port() const {
  return [hub = hub_, id = id_](StreamControl control) { return hub && hub->control(id, control); };
}

void MicrophoneHub::Consumer::detach() {
  if (MicrophoneHub* const hub = std::exchange(hub_, nullptr)) hub->detach(std::exchange(id_, 0));
}

MicrophoneHub::Consumer MicrophoneHub::attach(Deliver deliver) {
  const int id = nextId_;
  nextId_ = posix::nextInSequence(nextId_);
  consumers_[id].deliver = std::move(deliver);
  return Consumer(*this, id);
}

void MicrophoneHub::detach(int id) {
  consumers_.erase(id);
  releaseIfUnused();
}

bool MicrophoneHub::control(int id, const StreamControl& control) {
  auto it = consumers_.find(id);
  if (it == consumers_.end()) return false;
  ConsumerState& consumer = it->second;
  switch (control.operation) {
    case StreamControl::Probe:
      if (state_ != State::Idle && state_ != State::Probing) {
        support(id, control.epoch, true);
      } else {
        consumer.probeEpoch = control.epoch;
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
      if (consumer.probeEpoch == control.epoch) consumer.probeEpoch = 0;
      if (consumer.wanted && consumer.epoch == control.epoch) {
        finish(id, consumer, {});
        releaseIfUnused();
      }
      break;
  }
  return true;
}

void MicrophoneHub::receive(const StreamEvent& e, int64_t now) {
  now_ = now;
  if (!epoch_ || e.epoch != epoch_) return;
  switch (e.kind) {
    case StreamEvent::Kind::Support:
      if (state_ != State::Probing) break;
      answerProbes(e.available);
      flush();
      answerProbes(e.available);
      if (!e.available) {
        endUpstream(kStreamUnavailable);
      } else if (anyWanted()) {
        startUpstream();
      } else {
        state_ = State::Idle;
      }
      break;
    case StreamEvent::Kind::Started:
      if (state_ != State::Starting) break;
      state_ = State::Streaming;
      stateAt_ = now;
      history_.open();
      for (auto& [id, consumer] : consumers_)
        if (consumer.wanted) begin(id, consumer);
      break;
    case StreamEvent::Kind::Audio:
      if (state_ != State::Streaming) break;
      stateAt_ = now;
      if (tap_) tap_(e, now);
      history_.append(e.firstSample, e.samples);
      flush();
      deliverAudio(e);
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
      if (now - stateAt_ >= kStreamAnswerMs) endUpstream(kStreamNoAnswer);
      break;
    case State::Starting:
    case State::Streaming:
      if (now - stateAt_ >= kStreamAnswerMs) {
        const char* const error = state_ == State::Starting ? kStreamNoAnswer : kStalled;
        up(StreamControl::Stop);
        endUpstream(error);
      } else if (lingerUntil_ >= 0 && now >= lingerUntil_) {
        lingerUntil_ = -1;
        stopUpstream();
      } else if (now - keepaliveAt_ >= kStreamKeepaliveMs) {
        keepaliveAt_ = now;
        up(StreamControl::Keepalive);
      }
      break;
    case State::Stopping:
      if (now - stateAt_ >= kStreamAnswerMs) endUpstream({});
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
    consumer.probeEpoch = 0;
    if (consumer.wanted) finish(id, consumer, "microphone closed");
  }
  if (state_ == State::Starting || state_ == State::Streaming) stopUpstream();
}

MicrophoneMark MicrophoneHub::markOf(int id, uint32_t sample) const {
  const auto it = consumers_.find(id);
  if (state_ != State::Streaming || it == consumers_.end() || !it->second.started) return {};
  return {epoch_, it->second.firstUpstreamSample + sample};
}

void MicrophoneHub::startFrom(int id, MicrophoneMark mark) {
  const auto it = consumers_.find(id);
  if (it != consumers_.end()) it->second.startMark = mark;
}

bool MicrophoneHub::up(StreamControl::Operation operation) {
  return send_(encodeStreamControl({epoch_, operation}));
}

void MicrophoneHub::openUpstream(bool knownAvailable) {
  epoch_ = posix::nextInSequence(epoch_);
  if (knownAvailable) {
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

void MicrophoneHub::releaseIfUnused() {
  if (held_ || anyWanted()) return;
  if (state_ == State::Streaming && lingerUntil_ < 0) lingerUntil_ = now_ + kLingerMs;
  else if (state_ == State::Starting) stopUpstream();
}

void MicrophoneHub::endUpstream(const std::string& error) {
  const bool stopRequested = state_ == State::Stopping;
  state_ = State::Idle;
  lingerUntil_ = -1;
  history_.release();
  if (stopRequested) {
    if (anyWanted()) openUpstream(true);
    return;
  }
  for (auto& [id, consumer] : consumers_) {
    consumer.probeEpoch = 0;
    if (consumer.wanted) finish(id, consumer, error);
  }
}

void MicrophoneHub::answerProbes(bool available) {
  for (auto& [id, consumer] : consumers_) {
    if (!consumer.probeEpoch) continue;
    support(id, consumer.probeEpoch, available);
    consumer.probeEpoch = 0;
  }
}

void MicrophoneHub::begin(int id, ConsumerState& consumer) {
  const uint32_t back = consumer.startMark.epoch == epoch_ ? history_.backTo(consumer.startMark.sample) : 0;
  consumer.startMark = {};
  consumer.started = true;
  consumer.firstUpstreamSample = history_.next() - back;
  consumer.renewedAt = now_;
  StreamEvent started;
  started.kind = StreamEvent::Kind::Started;
  answer(id, consumer.epoch, std::move(started));
  for (uint32_t done = 0; done < back;) {
    const uint32_t count = std::min<uint32_t>(back - done, kStreamBlockSamples);
    StreamEvent held;
    held.kind = StreamEvent::Kind::Audio;
    held.firstSample = done;
    held.samples = history_.read(back - done, count);
    answer(id, consumer.epoch, std::move(held));
    done += count;
  }
}

void MicrophoneHub::finish(int id, ConsumerState& consumer, std::string error) {
  StreamEvent ended;
  ended.kind = StreamEvent::Kind::Ended;
  if (consumer.started) ended.firstSample = history_.next() - consumer.firstUpstreamSample;
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

void MicrophoneHub::deliverAudio(const StreamEvent& upstream) {
  audioTargets_.clear();
  for (const auto& [id, consumer] : consumers_)
    if (consumer.started) audioTargets_.emplace_back(id, consumer.epoch);
  if (audioTargets_.empty()) return;
  audio_.kind = StreamEvent::Kind::Audio;
  audio_.capturedMs = upstream.capturedMs;
  audio_.hostAtMs = upstream.hostAtMs;
  audio_.samples = upstream.samples;
  for (const auto& [id, epoch] : audioTargets_) {
    const auto it = consumers_.find(id);
    if (it == consumers_.end() || !it->second.started || it->second.epoch != epoch) continue;
    audio_.epoch = epoch;
    audio_.firstSample = upstream.firstSample - it->second.firstUpstreamSample;
    const Deliver deliver = it->second.deliver;
    deliver(audio_, now_);
  }
}

void MicrophoneHub::flush() {
  delivering_.swap(outbox_);
  for (auto& [id, event] : delivering_) {
    auto it = consumers_.find(id);
    if (it == consumers_.end()) continue;
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

}
