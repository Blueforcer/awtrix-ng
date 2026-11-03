#include "platform/tc002/speech/SpeechModelVoice.h"

#include <algorithm>
#include <atomic>
#include <vector>

#include "platform/tc002/speech/SpeechSynth.h"
#include "platform/tc002/speech/SpeechTokens.h"

namespace awtrix::speech {
namespace {

class ModelUtterance final : public SpeechUtterance {
 public:
  ModelUtterance(std::shared_ptr<const SpeechModel> model, const Plan& plan, const std::vector<Chunk>& chunks)
      : model_(std::move(model)), hold_(*model_), scratch_(*model_), acoustic_(*model_, scratch_),
        vocoder_(*model_, scratch_, acoustic_.maxFrames()), mel_(acoustic_.maxFrames() * model_->mels),
        samples_(vocoder_.maxSamples()), tokens_(plan.count + chunks.size()) {
    offsets_.push_back(0);
    for (const Chunk& chunk : chunks) offsets_.push_back(offsets_.back() + chunkTokens(plan, chunk, &tokens_[offsets_.back()]));
  }

  std::size_t render(int16_t* out, std::size_t max) override {
    while (at_ == available_) {
      if (cancelled_.load(std::memory_order_relaxed) || !step()) return 0;
    }
    if (cancelled_.load(std::memory_order_relaxed)) return 0;
    const std::size_t count = std::min(max, available_ - at_);
    std::copy_n(samples_.begin() + static_cast<std::ptrdiff_t>(at_), count, out);
    at_ += count;
    return count;
  }

  void cancel() override { cancelled_.store(true, std::memory_order_relaxed); }

 private:
  // Starts the next chunk or runs one step of the current one; false once every chunk is spoken.
  bool step() {
    at_ = available_ = 0;
    if (!speaking_) {
      if (chunk_ + 1 == offsets_.size()) return false;
      acoustic_.begin(&tokens_[offsets_[chunk_]], offsets_[chunk_ + 1] - offsets_[chunk_]);
      vocoder_.begin(acoustic_.frames());
      ++chunk_;
      speaking_ = true;
      return true;
    }
    const std::size_t frames = acoustic_.next(mel_.data());
    available_ = vocoder_.push(mel_.data(), frames, samples_.data());
    speaking_ = !acoustic_.done();
    return true;
  }

  const std::shared_ptr<const SpeechModel> model_;
  const SpeechModel::Hold hold_;
  SpeechScratch scratch_;
  AcousticStream acoustic_;
  VocoderStream vocoder_;
  std::vector<float> mel_;
  std::vector<int16_t> samples_;
  std::vector<Token> tokens_;
  // Chunk i holds tokens [offsets_[i], offsets_[i + 1]).
  std::vector<std::size_t> offsets_;
  std::size_t chunk_ = 0;
  bool speaking_ = false;
  std::size_t at_ = 0;
  std::size_t available_ = 0;
  std::atomic<bool> cancelled_{false};
};

}

std::shared_ptr<ModelVoice> ModelVoice::create(std::shared_ptr<const SpeechModel> model, std::string& error) {
  if (!model || model->frontendText.empty()) {
    error = "not a voice pack";
    return nullptr;
  }
  auto frontend = Frontend::parse(model->frontendText, model->frontendLexicon, error);
  if (!frontend) return nullptr;
  if (frontend->phoneCount() != model->phoneCount) {
    error = "the frontend's phones do not match the model";
    return nullptr;
  }
  return std::shared_ptr<ModelVoice>(new ModelVoice(std::move(model), std::move(frontend)));
}

std::unique_ptr<SpeechUtterance> ModelVoice::start(const Plan& plan) {
  if (!plan.count || !tokenizable(plan, model_->phoneCount)) return nullptr;
  std::vector<Chunk> chunks;
  splitPlan(plan, chunks);
  return std::make_unique<ModelUtterance>(model_, plan, chunks);
}

}
