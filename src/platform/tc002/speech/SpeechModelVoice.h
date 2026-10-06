#pragma once

#include <memory>

#include "platform/tc002/speech/SpeechModel.h"
#include "platform/tc002/speech/SpeechVoice.h"

namespace awtrix::speech {

// The voice of an ATTS model. Every utterance allocates its work space once in start() and then
// renders chunk by chunk, a step of frames at a time; cancel() takes effect at the next step.
class ModelVoice final : public SpeechVoice {
 public:
  explicit ModelVoice(std::shared_ptr<const SpeechModel> model) : model_(std::move(model)) {}

  uint32_t rate() const override { return model_->sampleRate; }
  std::unique_ptr<SpeechUtterance> start(const Plan& plan) override;

 private:
  std::shared_ptr<const SpeechModel> model_;
};

}
