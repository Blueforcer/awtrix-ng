#include "platform/tc002/contract/MicrophoneClient.h"

#include <utility>

#include "platform/posix/Sequence.h"

namespace awtrix::tc002 {

MicrophoneClient::MicrophoneClient(MicrophonePort port) : port_(std::move(port)) {}

void MicrophoneClient::send(StreamControl::Operation operation) { port_({epoch_, operation}); }

void MicrophoneClient::enter(Phase phase, int64_t now) {
  phase_ = phase;
  phaseAt_ = now;
}

void MicrophoneClient::open(int64_t now) {
  epoch_ = posix::nextInSequence(epoch_);
  error_.clear();
  enter(Phase::Probing, now);
  send(StreamControl::Probe);
}

void MicrophoneClient::stop(int64_t now) {
  switch (phase_) {
    case Phase::Idle:
    case Phase::Stopping:
      return;
    case Phase::Probing:
      cancel();
      return;
    case Phase::Starting:
    case Phase::Streaming:
      send(StreamControl::Stop);
      enter(Phase::Stopping, now);
      return;
  }
}

void MicrophoneClient::cancel() {
  if (phase_ != Phase::Idle && phase_ != Phase::Stopping) send(StreamControl::Stop);
  phase_ = Phase::Idle;
}

MicrophoneClient::Report MicrophoneClient::receive(const StreamEvent& event, int64_t now) {
  if (phase_ == Phase::Idle || event.epoch != epoch_) return Report::None;
  switch (event.kind) {
    case StreamEvent::Kind::Support:
      if (phase_ != Phase::Probing) return Report::None;
      if (!event.available) {
        phase_ = Phase::Idle;
        error_ = kStreamUnavailable;
        return Report::Unavailable;
      }
      enter(Phase::Starting, now);
      keepaliveAt_ = now;
      send(StreamControl::Start);
      return Report::None;
    case StreamEvent::Kind::Started:
      if (phase_ != Phase::Starting) return Report::None;
      enter(Phase::Streaming, now);
      expected_ = 0;
      return Report::Started;
    case StreamEvent::Kind::Audio: {
      if (phase_ != Phase::Streaming) return Report::None;
      const bool gap = event.firstSample != expected_;
      expected_ = event.firstSample + static_cast<uint32_t>(event.samples.size());
      return gap ? Report::Gap : Report::Audio;
    }
    case StreamEvent::Kind::Ended: {
      const bool asked = phase_ == Phase::Stopping;
      phase_ = Phase::Idle;
      error_ = event.error;
      return asked ? Report::Stopped : Report::Ended;
    }
  }
  return Report::None;
}

MicrophoneClient::Report MicrophoneClient::tick(int64_t now) {
  switch (phase_) {
    case Phase::Idle:
      return Report::None;
    case Phase::Stopping:
      if (now - phaseAt_ < kStreamAnswerMs) return Report::None;
      phase_ = Phase::Idle;
      error_.clear();
      return Report::Stopped;
    case Phase::Probing:
    case Phase::Starting:
      if (now - phaseAt_ < kStreamAnswerMs) break;
      cancel();
      error_ = kStreamNoAnswer;
      return Report::Ended;
    case Phase::Streaming:
      break;
  }
  if (phase_ != Phase::Probing && now - keepaliveAt_ >= kStreamKeepaliveMs) {
    keepaliveAt_ = now;
    send(StreamControl::Keepalive);
  }
  return Report::None;
}

}
