#pragma once

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <string>
#include <utility>
#include <vector>

#include "../../support.h"
#include "platform/tc002/runtime/MicrophoneHub.h"

namespace awtrix::test {

class FakeMicrophoneDaemon {
 public:
  using Event = tc002::StreamEvent;
  using Control = tc002::StreamControl;

  std::vector<Control> controls;
  bool linkUp = true;
  uint32_t sample = 0;

  tc002::MicrophoneHub::Send link() {
    return [this](std::string wire) {
      Control control;
      if (tc002::decodeStreamControl(wire, control)) controls.push_back(control);
      return linkUp;
    };
  }

  const Control& last() const {
    require(!controls.empty(), "the hub has sent a control upstream");
    return controls.back();
  }

  unsigned count(Control::Operation operation) const {
    unsigned n = 0;
    for (const Control& control : controls) n += control.operation == operation;
    return n;
  }

  Event event(Event::Kind kind, bool available = true, std::string error = {}) {
    return answer(last().epoch, kind, available, std::move(error));
  }

  Event audio(std::vector<int16_t> samples, int64_t at) {
    Event e = answer(stream_, Event::Kind::Audio);
    e.firstSample = sample;
    e.hostAtMs = at;
    sample += static_cast<uint32_t>(samples.size());
    e.samples = std::move(samples);
    return e;
  }

  static std::vector<std::vector<int16_t>> blocks(const std::vector<int16_t>& pcm) {
    std::vector<std::vector<int16_t>> out;
    for (std::size_t at = 0; at < pcm.size(); at += tc002::kStreamBlockSamples) {
      const std::size_t end = std::min<std::size_t>(pcm.size(), at + tc002::kStreamBlockSamples);
      out.emplace_back(pcm.begin() + static_cast<std::ptrdiff_t>(at), pcm.begin() + static_cast<std::ptrdiff_t>(end));
    }
    return out;
  }

  Event ramp(int64_t at) {
    std::vector<int16_t> samples(tc002::kStreamBlockSamples);
    for (std::size_t i = 0; i < samples.size(); ++i) samples[i] = static_cast<int16_t>((sample + i) % 32768);
    return audio(std::move(samples), at);
  }

  std::vector<Event> answers(int64_t at) {
    std::vector<Event> out;
    for (; answered_ < controls.size(); ++answered_) {
      const Control control = controls[answered_];
      Event::Kind kind = Event::Kind::Support;
      if (control.operation == Control::Start) kind = Event::Kind::Started;
      else if (control.operation == Control::Stop) kind = Event::Kind::Ended;
      else if (control.operation == Control::Keepalive) continue;
      out.push_back(answer(control.epoch, kind));
      out.back().hostAtMs = at;
    }
    return out;
  }

  Event end(std::string error, int64_t at) {
    Event e = answer(stream_, Event::Kind::Ended, true, std::move(error));
    e.hostAtMs = at;
    return e;
  }

  bool streaming() const { return streaming_; }

 private:
  Event answer(int epoch, Event::Kind kind, bool available = true, std::string error = {}) {
    Event e;
    e.epoch = epoch;
    e.kind = kind;
    e.available = available;
    e.error = std::move(error);
    if (kind == Event::Kind::Started) {
      stream_ = epoch;
      sample = 0;
      streaming_ = true;
    } else if (kind == Event::Kind::Ended) {
      e.firstSample = sample;
      streaming_ = false;
    }
    return e;
  }

  std::size_t answered_ = 0;
  int stream_ = 0;
  bool streaming_ = false;
};

}
