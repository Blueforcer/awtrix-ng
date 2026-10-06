#pragma once

#include <cstddef>
#include <cstdint>
#include <memory>

#include "platform/tc002/audio/Tc002AudioSources.h"
#include "platform/tc002/speech/SpeechVoice.h"

namespace awtrix::speech {

// A plan read aloud for the mixer. A worker thread of its own renders it into a ring of kBufferMs,
// so the audio thread never waits for the voice: next() answers Wait until kPrerollMs are in or
// the voice is done, and whenever the voice falls behind. Dropping the source cancels the
// utterance and returns at once; the worker ends by itself, and shutdown waits for it.
class SpeechSource final : public tc002::PcmSource {
 public:
  static constexpr uint32_t kBufferMs = 1000;
  static constexpr uint32_t kPrerollMs = 150;
  static constexpr std::size_t kBlockFrames = 512;

  // Returns at once; nullptr when no worker starts.
  static std::unique_ptr<SpeechSource> open(std::shared_ptr<SpeechVoice> voice, std::shared_ptr<const Plan> plan);
  ~SpeechSource() override;
  SpeechSource(const SpeechSource&) = delete;
  SpeechSource& operator=(const SpeechSource&) = delete;

  Read next(const int16_t*& samples, std::size_t& frames) override;
  uint32_t rate() const override { return rate_; }
  uint8_t channels() const override { return 1; }

  // Workers outlive their source until the voice's render() returns; shutdown waits here.
  static bool waitForWorkers(int timeoutMs);

  struct Shared;

 private:
  SpeechSource(std::shared_ptr<Shared> shared, uint32_t rate);

  std::shared_ptr<Shared> shared_;
  uint32_t rate_;
  std::size_t preroll_;
  bool started_ = false;
  int16_t block_[kBlockFrames]{};
};

}
