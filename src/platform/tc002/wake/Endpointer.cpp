#include "platform/tc002/wake/Endpointer.h"

namespace awtrix::tc002::wake {
namespace {
constexpr uint64_t kSamplesPerMs = 16;
}  // namespace

void EndpointTimer::start(const EndpointSettings& settings, uint64_t from) {
  settings_ = settings;
  from_ = start_ = end_ = from;
  speaking_ = done_ = false;
}

Endpoint EndpointTimer::step(bool voice, uint64_t at) {
  if (done_ || at < from_) return Endpoint::None;
  if (voice) {
    end_ = at;
    if (!speaking_) {
      speaking_ = true;
      start_ = at;
      return Endpoint::Started;
    }
  }
  if (speaking_ && !voice && (at - end_) / kSamplesPerMs >= settings_.silenceMs) return finish(Endpoint::Ended);
  const uint64_t ms = (at - from_) / kSamplesPerMs;
  if (ms >= settings_.maxMs) return finish(speaking_ ? Endpoint::Limit : Endpoint::Silence);
  if (!speaking_ && ms >= settings_.waitMs) return finish(Endpoint::Silence);
  return Endpoint::None;
}

Endpoint EndpointTimer::finish(Endpoint kind) {
  done_ = true;
  return kind;
}

bool Endpointer::load(std::string_view model, float cutoff, std::size_t window, std::string& error) {
  // Without a cool-off, WakeWindow says for every step whether the window mean reaches the cutoff. Unlike
  // the wake word, the model needs no warm-up after a reset: on noise of -50 to -20 dBFS its first steps
  // stay near 0.
  WakeWindow verdict;
  if (!verdict.configure({cutoff, window, 0, 0}, error)) return false;
  if (!stream_.load(model, error)) return false;
  window_ = verdict;
  return true;
}

void Endpointer::start(const EndpointSettings& settings, uint64_t from) {
  stream_.reset();
  window_.reset();
  timer_.start(settings, from);
}

Endpoint Endpointer::next(const int16_t*& samples, std::size_t& count) {
  uint8_t probability = 0;
  while (!timer_.done() && stream_.next(samples, count, probability)) {
    WakeEvent unused;
    const bool voice = window_.push(probability, stream_.position(), unused);
    const Endpoint kind = timer_.step(voice, stream_.position());
    if (kind != Endpoint::None) return kind;
  }
  return Endpoint::None;
}

}
