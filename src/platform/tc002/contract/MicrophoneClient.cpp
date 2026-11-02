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

void MicrophoneClient::renew() {
  epoch_ = posix::nextInSequence(epoch_);
  error_.clear();
  started_ = false;
}

void MicrophoneClient::requestStart(int64_t now) {
  enter(Phase::Starting, now);
  keepaliveAt_ = now;
  send(StreamControl::Start);
}

void MicrophoneClient::open(int64_t now) {
  renew();
  enter(Phase::Opening, now);
  send(StreamControl::Probe);
}

void MicrophoneClient::probe() {
  renew();
  probeAgain();
}

void MicrophoneClient::probeAgain() {
  phase_ = Phase::Probing;
  send(StreamControl::Probe);
}

void MicrophoneClient::start(int64_t now) {
  renew();
  requestStart(now);
}

void MicrophoneClient::stop(int64_t now) {
  switch (phase_) {
    case Phase::Idle:
    case Phase::Stopping:
      return;
    case Phase::Probing:
    case Phase::Opening:
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
  if (phase_ != Phase::Idle && phase_ != Phase::Probing && phase_ != Phase::Stopping) send(StreamControl::Stop);
  phase_ = Phase::Idle;
}

MicrophoneClient::Report MicrophoneClient::receive(const StreamEvent& event, int64_t now) {
  if (phase_ == Phase::Idle || event.epoch != epoch_) return Report::None;
  switch (event.kind) {
    case StreamEvent::Kind::Support:
      if (phase_ == Phase::Probing) return event.available ? Report::Available : Report::Unavailable;
      if (phase_ != Phase::Opening) return Report::None;
      if (!event.available) {
        phase_ = Phase::Idle;
        error_ = kStreamUnavailable;
        return Report::Unavailable;
      }
      requestStart(now);
      return Report::None;
    case StreamEvent::Kind::Started:
      if (phase_ == Phase::Stopping) started_ = true;
      if (phase_ != Phase::Starting) return Report::None;
      started_ = true;
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
      if (phase_ == Phase::Probing) return Report::None;
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
    case Phase::Probing:
      return Report::None;
    case Phase::Stopping:
      if (now - phaseAt_ < kStreamAnswerMs) return Report::None;
      phase_ = Phase::Idle;
      error_.clear();
      return Report::Stopped;
    case Phase::Opening:
    case Phase::Starting:
      if (now - phaseAt_ < kStreamAnswerMs) break;
      cancel();
      error_ = kStreamNoAnswer;
      return Report::Ended;
    case Phase::Streaming:
      break;
  }
  if (phase_ != Phase::Opening && now - keepaliveAt_ >= kStreamKeepaliveMs) {
    keepaliveAt_ = now;
    send(StreamControl::Keepalive);
  }
  return Report::None;
}

}
