#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>
#include <string_view>

#include "platform/tc002/wake/WakeFrontend.h"
#include "platform/tc002/wake/WakeModel.h"

namespace awtrix::tc002::wake {

// A microWakeWord streaming model on 16 kHz PCM: the frontend's features, quantised exactly as the
// trainer fed them, and one model invoke every N frames. Never allocates in next(); not thread-safe.
class ModelStream {
 public:
  // A .tflite file with one uint8 output. False and a reason otherwise, keeping the model there was.
  // The stream starts over.
  bool load(std::string_view bytes, std::string& error);
  bool loaded() const { return model_ != nullptr; }
  // Takes samples from the front of [samples, samples + count) until the model has run once and
  // advances both: true with its output. False when the samples ran out first; they are kept for the
  // next call. Without a model it takes nothing.
  bool next(const int16_t*& samples, std::size_t& count, uint8_t& probability);
  // Samples taken since reset; after a true next(), the end of the frames the model ran on.
  uint64_t position() const { return position_; }
  // Back to a fresh stream: frontend, model state and position.
  void reset();
  // nullptr before load().
  const WakeModel* model() const { return model_.get(); }

 private:
  std::unique_ptr<WakeModel> model_;
  WakeFrontend frontend_;
  std::array<int8_t, 128> quantized_{};
  std::size_t frame_ = 0;
  uint64_t position_ = 0;
};

}
