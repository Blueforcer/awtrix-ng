#include "platform/tc002/wake/Endpointer.h"

namespace awtrix::tc002::wake {
namespace {
constexpr uint64_t kSamplesPerMs = WakeFrontend::kSamplesPerMs;
constexpr uint32_t kNoCooloffMs = 0, kNoWarmupMs = 0;
}

void EndpointTimer::start(const EndpointSettings& settings) {
  settings_ = settings;
  end_ = 0;
  speaking_ = done_ = false;
}

Endpoint EndpointTimer::step(bool voice, uint64_t at) {
  if (done_) return Endpoint::None;
  if (voice) {
    end_ = at;
    if (!speaking_) {
      speaking_ = true;
      return Endpoint::Started;
    }
  }
  if (speaking_ && !voice && (at - end_) / kSamplesPerMs >= settings_.silenceMs) return finish(Endpoint::Ended);
  const uint64_t ms = at / kSamplesPerMs;
  if (ms >= settings_.maxMs) return finish(speaking_ ? Endpoint::Limit : Endpoint::Silence);
  if (!speaking_ && ms >= settings_.waitMs) return finish(Endpoint::Silence);
  return Endpoint::None;
}

Endpoint EndpointTimer::finish(Endpoint kind) {
  done_ = true;
  return kind;
}

bool Endpointer::load(std::string_view model, std::string& error) { return stream_.load(model, error); }

bool Endpointer::configure(float cutoff, std::size_t window, std::string& error) {
  return window_.configure({cutoff, window, kNoCooloffMs, kNoWarmupMs}, error);
}

void Endpointer::start(const EndpointSettings& settings) {
  stream_.reset();
  window_.reset();
  timer_.start(settings);
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
