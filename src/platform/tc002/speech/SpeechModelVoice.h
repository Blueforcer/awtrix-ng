#pragma once

#include <memory>
#include <string>

#include "platform/tc002/speech/SpeechFrontend.h"
#include "platform/tc002/speech/SpeechModel.h"
#include "platform/tc002/speech/SpeechVoice.h"

namespace awtrix::speech {

// The voice of a voice pack: its model and its frontend. Every utterance allocates its work space
// once in start() and then renders chunk by chunk, a step of frames at a time; cancel() takes
// effect at the next step.
class ModelVoice final : public SpeechVoice {
 public:
  // nullptr and the reason when the model carries no frontend or one whose phones it lacks.
  static std::shared_ptr<ModelVoice> create(std::shared_ptr<const SpeechModel> model, std::string& error);

  uint32_t rate() const override { return model_->sampleRate; }
  bool prepare(std::string_view text, Plan& out) const override { return frontend_->prepare(text, out); }
  std::unique_ptr<SpeechUtterance> start(const Plan& plan) override;

  const SpeechModel& model() const { return *model_; }

 private:
  ModelVoice(std::shared_ptr<const SpeechModel> model, std::unique_ptr<const Frontend> frontend)
      : model_(std::move(model)), frontend_(std::move(frontend)) {}

  std::shared_ptr<const SpeechModel> model_;
  std::unique_ptr<const Frontend> frontend_;
};

}
