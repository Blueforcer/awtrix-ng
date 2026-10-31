#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>

#include "platform/tc002/wake/ModelStream.h"
#include "platform/tc002/wake/WakeDetector.h"

namespace awtrix::tc002::wake {

// How long a recording waits and lasts, in milliseconds of audio.
struct EndpointSettings {
  uint32_t silenceMs = 800;  // speech is over after this long without voice
  uint32_t waitMs = 5000;    // no speech by then: the recording heard nothing
  uint32_t maxMs = 15000;    // the recording ends here in any case
};

// What a recording reached. Ended, Silence and Limit end it.
enum class Endpoint { None, Started, Ended, Silence, Limit };

// The decisions of a recording from one voice verdict per model step, by stream position in samples
// at 16 kHz.
class EndpointTimer {
 public:
  // A new recording whose timing counts from stream position `from`; verdicts before it are ignored.
  void start(const EndpointSettings& settings, uint64_t from);
  // The verdict of the model step that ended at stream position `at`. None after an end, and before
  // the first start.
  Endpoint step(bool voice, uint64_t at);
  bool done() const { return done_; }
  // The first and the last step with voice; valid after Started.
  uint64_t speechStart() const { return start_; }
  uint64_t speechEnd() const { return end_; }

 private:
  Endpoint finish(Endpoint kind);

  EndpointSettings settings_;
  uint64_t from_ = 0, start_ = 0, end_ = 0;
  bool speaking_ = false, done_ = true;
};

// Where speech in a recording begins and ends: a voice activity model on the wake-word engine, its
// window mean against its cutoff as the verdict of each step, and EndpointTimer. Never allocates in
// next(); not thread-safe.
class Endpointer {
 public:
  // A voice activity model (.tflite, one uint8 output) with the cutoff (0..1) and window (1..64) of its
  // settings. False and a reason otherwise, keeping what was loaded.
  bool load(std::string_view model, float cutoff, std::size_t window, std::string& error);
  // A new recording on a fresh stream. Audio before stream position `from` only warms up the model;
  // the timing counts from there.
  void start(const EndpointSettings& settings, uint64_t from = 0);
  // Takes samples until the next event and advances both: the event, or None once the samples ran
  // out. After Ended, Silence or Limit, and before start(), it takes nothing.
  Endpoint next(const int16_t*& samples, std::size_t& count);
  uint64_t speechStart() const { return timer_.speechStart(); }
  uint64_t speechEnd() const { return timer_.speechEnd(); }
  // Samples since start().
  uint64_t position() const { return stream_.position(); }
  // Audio per model step in ms; 0 before load().
  uint64_t stepMs() const { return stream_.model() ? stream_.model()->frames() * 10 : 0; }

 private:
  ModelStream stream_;
  WakeWindow window_;
  EndpointTimer timer_;
};

}
