#pragma once

#include "core/sound/Rtttl.h"
#include <utility>

namespace awtrix::sound {

// Portable, sample-clocked RTTTL player. No wall clock, allocation or floating
// point in next(): buffering cannot change note lengths or insert note gaps.
class ToneSamples {
 public:
  static constexpr uint32_t SampleRate = 24000;
  bool play(const std::string& text) {
    auto parsed = rtttl::parse(text);
    if (!parsed.ok) return false; // Invalid requests do not interrupt playback.
    melody_ = std::move(parsed);
    index_ = 0;
    loadNote();
    return true;
  }
  void stop() { index_ = melody_.notes.size(); remaining_ = 0; }
  bool playing() const { return remaining_ != 0; }
  void setVolume(uint8_t percent) { amplitude_ = 32767 * (percent > 100 ? 100 : percent) / 100; }
  int16_t next() {
    if (!playing()) return 0;
    const int16_t sample = frequency_ && remaining_ > gap_
        ? static_cast<int16_t>(phase_ < SampleRate / 2 ? amplitude_ : -amplitude_) : 0;
    phase_ = (phase_ + frequency_) % SampleRate;
    if (--remaining_ == 0) { ++index_; loadNote(); }
    return sample;
  }
  static uint32_t stereo(int16_t sample) {
    const uint32_t bits = static_cast<uint16_t>(sample);
    return (bits << 16) | bits;
  }
 private:
  void loadNote() {
    if (index_ == melody_.notes.size()) return;
    const auto& note = melody_.notes[index_];
    const auto ms = rtttl::noteMs(note.duration, melody_.timeUnit);
    remaining_ = ms * (SampleRate / 1000);
    gap_ = note.frequency && ms > 12 ? 6 * (SampleRate / 1000) : 0;
    frequency_ = note.frequency;
    phase_ = 0;
  }
  rtttl::Parse melody_;
  size_t index_ = 0;
  uint32_t remaining_ = 0, gap_ = 0, phase_ = 0, frequency_ = 0;
  int amplitude_ = 0;
};
}
