#include "platform/tc002/audio/Tc002AudioLink.h"

#include <errno.h>
#include <fcntl.h>
#include <sys/socket.h>
#include <unistd.h>

#include <algorithm>

namespace awtrix {
namespace tc002 {
namespace {

const char* errorName(uint8_t error) {
  switch (error) {
    case TC002_AUDIO_ERROR_PREFLIGHT: return "the speaker is in use or this is not a TC002";
    case TC002_AUDIO_ERROR_VENDOR: return "the audio driver reported an error";
    case TC002_AUDIO_ERROR_QUEUE: return "the audio driver reported an inconsistent queue";
    default: return "the speaker helper failed";
  }
}

}

Tc002AudioLink::Tc002AudioLink(int fd) : fd_(fd) {
  if (fd_ < 0) {
    fail("no speaker helper");
    return;
  }
  const int flags = ::fcntl(fd_, F_GETFL);
  if (flags < 0 || ::fcntl(fd_, F_SETFL, flags | O_NONBLOCK) < 0 ||
      ::fcntl(fd_, F_SETFD, FD_CLOEXEC) < 0)
    fail("the speaker socket is unusable");
}

Tc002AudioLink::~Tc002AudioLink() {
  if (fd_ >= 0) ::close(fd_);
}

void Tc002AudioLink::fail(const std::string& why) {
  if (failed_) return;
  failed_ = true;
  failure_ = why;
  open_ = false;
  control_.clear();
}

void Tc002AudioLink::failSocket() {
  fail(errno == ECONNRESET || errno == EPIPE ? "the speaker helper exited" : "the speaker socket broke");
}

void Tc002AudioLink::poll() {
  uint8_t message[TC002_AUDIO_MAX_MESSAGE + 1];
  while (!failed_) {
    const ssize_t n = ::recv(fd_, message, sizeof message, MSG_DONTWAIT | MSG_TRUNC);
    if (n == 0) {
      fail("the speaker helper exited");
      return;
    }
    if (n < 0) {
      if (errno == EINTR) continue;
      if (errno != EAGAIN && errno != EWOULDBLOCK) failSocket();
      break;
    }
    if (static_cast<std::size_t>(n) > TC002_AUDIO_MAX_MESSAGE) continue;
    receive(message, static_cast<std::size_t>(n));
  }
  flushControl();
}

void Tc002AudioLink::receive(const uint8_t* data, std::size_t size) {
  tc002_audio_frame frame;
  if (!tc002_audio_parse(data, size, &frame)) return;
  if (frame.type == TC002_AUDIO_HELLO) {
    tc002_audio_hello hello;
    if (!tc002_audio_read_hello(&frame, &hello) || hello.device_rate == 0) {
      fail("the speaker helper speaks another protocol");
      return;
    }
    info_ = hello;
    hello_ = true;
    return;
  }
  if (frame.type != TC002_AUDIO_STATUS) return;
  tc002_audio_status status;
  if (!tc002_audio_read_status(&frame, &status)) return;
  status_ = status;
  if (status.state == TC002_AUDIO_FAILED) {
    fail(errorName(status.error));
    return;
  }
  if (open_ && frame.generation == generation_) consumed_ = status.consumed_bytes;
}

void Tc002AudioLink::queue(const uint8_t* message, std::size_t size) {
  if (failed_ || !size) return;
  control_.emplace_back(message, message + size);
  flushControl();
}

void Tc002AudioLink::flushControl() {
  while (!failed_ && !control_.empty()) {
    const std::vector<uint8_t>& message = control_.front();
    const ssize_t n = ::send(fd_, message.data(), message.size(), MSG_DONTWAIT | MSG_NOSIGNAL);
    if (n < 0) {
      if (errno == EINTR) continue;
      if (errno != EAGAIN && errno != EWOULDBLOCK && errno != ENOBUFS)
        failSocket();
      return;
    }
    control_.pop_front();
  }
}

uint32_t Tc002AudioLink::open(uint32_t rate, uint8_t channels) {
  uint8_t message[TC002_AUDIO_MAX_MESSAGE];
  if (failed_) return 0;
  if (++generation_ == 0) ++generation_;
  rate_ = rate;
  channels_ = channels;
  sent_ = 0;
  consumed_ = 0;
  open_ = true;
  draining_ = false;
  queue(message, tc002_audio_encode_open(message, sizeof message, generation_, rate, channels));
  return generation_;
}

std::size_t Tc002AudioLink::credit() const {
  if (!ready() || !open_ || draining_ || !control_.empty()) return 0;
  const std::size_t window = std::min<std::size_t>(info_.window_bytes, TC002_AUDIO_WINDOW_BYTES);
  const std::size_t inFlight = static_cast<uint32_t>(sent_ - consumed_);
  if (inFlight >= window) return 0;
  return std::min<std::size_t>(window - inFlight, info_.max_pcm_bytes);
}

bool Tc002AudioLink::sendPcm(const int16_t* samples, std::size_t count) {
  uint8_t message[TC002_AUDIO_MAX_MESSAGE];
  if (!count || count * 2 > credit() || count % channels_) return false;
  const std::size_t size =
      tc002_audio_encode_pcm(message, sizeof message, generation_, samples, count);
  if (!size) return false;
  for (;;) {
    const ssize_t n = ::send(fd_, message, size, MSG_DONTWAIT | MSG_NOSIGNAL);
    if (n >= 0) break;
    if (errno == EINTR) continue;
    if (errno != EAGAIN && errno != EWOULDBLOCK && errno != ENOBUFS) failSocket();
    return false;
  }
  sent_ += static_cast<uint32_t>(count * 2);
  return true;
}

void Tc002AudioLink::drain() {
  uint8_t message[TC002_AUDIO_HEADER_BYTES];
  if (!open_ || draining_) return;
  draining_ = true;
  queue(message, tc002_audio_encode_empty(message, sizeof message, TC002_AUDIO_DRAIN, generation_));
}

void Tc002AudioLink::stop() {
  uint8_t message[TC002_AUDIO_HEADER_BYTES];
  if (!open_) return;
  open_ = false;
  draining_ = false;
  queue(message, tc002_audio_encode_empty(message, sizeof message, TC002_AUDIO_STOP, generation_));
}

void Tc002AudioLink::setVolume(uint8_t percent) {
  uint8_t message[TC002_AUDIO_HEADER_BYTES + TC002_AUDIO_VOLUME_BYTES];
  queue(message, tc002_audio_encode_volume(message, sizeof message, percent > 100 ? 100 : percent));
}

bool Tc002AudioLink::finished(uint32_t generation) const {
  const uint32_t completed = status_.completed_generation;
  return generation && completed && static_cast<int32_t>(completed - generation) >= 0;
}

uint32_t Tc002AudioLink::queuedMs() const {
  uint32_t ms = status_.device_busy_bytes * 10u / 882u;
  if (open_ && rate_ && channels_)
    ms += static_cast<uint32_t>(static_cast<uint64_t>(static_cast<uint32_t>(sent_ - consumed_)) *
                                1000u / (rate_ * channels_ * 2u));
  return ms;
}

}
}
