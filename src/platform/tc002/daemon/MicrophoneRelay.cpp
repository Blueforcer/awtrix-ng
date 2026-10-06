#include "platform/tc002/daemon/MicrophoneRelay.h"

#include <cerrno>
#include <cstring>
#include <sys/socket.h>

#include "platform/tc002/daemon/Log.h"
#include "platform/tc002/daemon/RuntimeLinks.h"

namespace awtrix::tc002d {

void MicrophoneRelay::requestPcm(int id, int64_t nowMs) {
  if (!peer_.hello || !peer_.channel.valid()) return;
  // The runtime contract allows one outstanding request. Extra requests must not displace
  // its completion, grow a queue, or start another capture while the channel is congested.
  if (pcmPending_ || !pcmReply_.empty()) return;
  if (!links_.microphonePcm || (pcmRequestAt_ >= 0 && nowMs - pcmRequestAt_ < 100)) {
    pcmReply_ = tc002::encodeMicrophonePcm({id, {}, "microphone unavailable or busy"});
    flushPcm();
    return;
  }
  pcmPending_ = true;
  pcmRequestAt_ = nowMs;
  const std::weak_ptr<char> alive = peer_.alive;
  const unsigned generation = peer_.generation;
  links_.microphonePcm(id, [this, alive, generation](const tc002::MicrophonePcm& pcm) {
    if (alive.expired()) return;
    pcmPending_ = false;
    if (generation != peer_.generation || peer_.pid <= 0 || !peer_.channel.valid()) return;
    pcmReply_ = tc002::encodeMicrophonePcm(pcm);
    flushPcm();
  });
}

void MicrophoneRelay::flushPcm() {
  if (pcmReply_.empty() || !peer_.channel.valid() || !peer_.hello) return;
  ssize_t sent;
  do {
    sent = ::send(peer_.channel.get(), pcmReply_.data(), pcmReply_.size(), MSG_DONTWAIT | MSG_NOSIGNAL);
  } while (sent < 0 && errno == EINTR);
  if (sent < 0 && (errno == EAGAIN || errno == EWOULDBLOCK || errno == ENOBUFS)) return;
  if (sent < 0 && peer_.sendErrors++ < 5)
    Log::line("runtime", "PCM reply to runtime failed: %s", std::strerror(errno));
  pcmReply_.clear();
}

void MicrophoneRelay::revokeStream() {
  const int epoch = streamEpoch_;
  streamEpoch_ = 0;
  streamReplies_.clear();
  if (epoch && links_.microphoneStreamControl) links_.microphoneStreamControl(epoch, true);
}

bool MicrophoneRelay::queueStream(const tc002::StreamEvent& event) {
  if (!peer_.channel.valid() || !peer_.hello) return false;
  if (streamReplies_.size() >= 8) {
    // One reserved terminal slot: never discard accepted audio on a clean end,
    // and a rejected foreign start must not displace the active session's data.
    if (streamReplies_.size() >= 9 || event.kind != tc002::StreamEvent::Kind::Ended ||
        event.epoch != streamEpoch_) return false;
  }
  auto encoded = tc002::encodeStreamEvent(event);
  if (encoded.empty()) return false;
  streamReplies_.push_back(std::move(encoded));
  flushStream();
  return peer_.channel.valid();
}

void MicrophoneRelay::flushStream() {
  while (peer_.channel.valid() && !streamReplies_.empty()) {
    const auto& message = streamReplies_.front();
    const ssize_t n = ::send(peer_.channel.get(), message.data(), message.size(), MSG_DONTWAIT | MSG_NOSIGNAL);
    if (n < 0 && errno == EINTR) continue;
    if (n < 0 && (errno == EAGAIN || errno == EWOULDBLOCK || errno == ENOBUFS)) return;
    if (n != static_cast<ssize_t>(message.size())) { revokeStream(); return; }
    streamReplies_.pop_front();
  }
}

void MicrophoneRelay::requestStream(const tc002::StreamControl& control, int64_t nowMs) {
  if (!peer_.hello || !peer_.channel.valid()) return;
  using Control = tc002::StreamControl;
  if (control.operation == Control::Probe) {
    tc002::StreamEvent event;
    event.epoch = control.epoch; event.kind = tc002::StreamEvent::Kind::Support;
    event.hostAtMs = nowMs;
    event.available = links_.microphoneStreamAvailable && links_.microphoneStreamAvailable();
    queueStream(event);
    return;
  }
  if (control.operation != Control::Start) {
    if (control.epoch == streamEpoch_ && links_.microphoneStreamControl)
      links_.microphoneStreamControl(control.epoch, control.operation == Control::Stop);
    return;
  }
  if (streamEpoch_ == control.epoch) return;
  bool accepted = false;
  if (!streamEpoch_ && streamReplies_.empty() && links_.microphoneStreamStart) {
    streamEpoch_ = control.epoch;
    const std::weak_ptr<char> alive = peer_.alive;
    const unsigned generation = peer_.generation;
    accepted = links_.microphoneStreamStart(control.epoch, [this, alive, generation](const tc002::StreamEvent& event) {
      if (alive.expired() || generation != peer_.generation || event.epoch != streamEpoch_) return false;
      const bool delivered = queueStream(event);
      if (event.kind == tc002::StreamEvent::Kind::Ended) streamEpoch_ = 0;
      return delivered;
    });
    if (!accepted) streamEpoch_ = 0;
  }
  if (!accepted) {
    tc002::StreamEvent event;
    event.epoch = control.epoch; event.kind = tc002::StreamEvent::Kind::Ended;
    event.hostAtMs = nowMs; event.error = "microphone unavailable or busy";
    queueStream(event);
  }
}

}  // namespace awtrix::tc002d
