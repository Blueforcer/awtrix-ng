#pragma once

#include <cstddef>
#include <cstdint>
#include <memory>

#include "platform/tc002/speech/SpeechTypes.h"

namespace awtrix::speech {

// One utterance of a voice. The worker thread that started it calls render() until it returns 0.
class SpeechUtterance {
 public:
  virtual ~SpeechUtterance() = default;
  // Writes up to max mono samples at the voice's rate and returns how many; 0 once the utterance
  // is over, spoken to its end, failed or cancelled.
  virtual std::size_t render(int16_t* out, std::size_t max) = 0;
  // From any thread, also while render() runs, and without waiting: render() returns soon, and 0
  // from then on.
  virtual void cancel() = 0;
};

// What reads a plan aloud. Every utterance has a worker thread of its own, so start() and render()
// of two utterances may run at once; only what never changes may be shared between them.
class SpeechVoice {
 public:
  virtual ~SpeechVoice() = default;
  virtual uint32_t rate() const = 0;
  // On the utterance's worker thread; nullptr when the plan cannot be spoken.
  virtual std::unique_ptr<SpeechUtterance> start(const Plan& plan) = 0;
};

}
