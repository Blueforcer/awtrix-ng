#include "platform/tc002/wake/ModelStream.h"

#include <algorithm>
#include <cmath>

namespace awtrix::tc002::wake {

static_assert(WakeModel::kFeatures == WakeFrontend::kChannels, "the model takes the frontend's channels");

bool ModelStream::load(std::string_view bytes, std::string& error) {
  std::unique_ptr<WakeModel> model =
    WakeModel::load(reinterpret_cast<const uint8_t*>(bytes.data()), bytes.size(), error);
  if (!model) return false;
  for (std::size_t k = 0; k < quantized_.size(); ++k) {
    const float q = std::nearbyint(static_cast<float>(k) / model->inputScale()) +
                    static_cast<float>(model->inputZeroPoint());
    quantized_[k] = static_cast<int8_t>(std::max(-128.0f, std::min(127.0f, q)));
  }
  model_ = std::move(model);
  reset();
  return true;
}

bool ModelStream::next(const int16_t*& samples, std::size_t& count, uint8_t& probability) {
  if (!model_) return false;
  WakeFrontend::Frame frame;
  for (;;) {
    const std::size_t before = count;
    const bool ready = frontend_.next(samples, count, frame);
    position_ += before - count;
    if (!ready) return false;
    int8_t* row = model_->input() + frame_ * WakeModel::kFeatures;
    for (std::size_t i = 0; i < WakeModel::kFeatures; ++i) row[i] = quantized_[trainerFeature(frame[i])];
    if (++frame_ < model_->frames()) continue;
    frame_ = 0;
    probability = model_->invoke();
    return true;
  }
}

void ModelStream::reset() {
  frontend_.reset();
  if (model_) model_->reset();
  frame_ = 0;
  position_ = 0;
}

}
