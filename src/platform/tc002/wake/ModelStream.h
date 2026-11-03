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

class ModelStream {
 public:
  bool load(std::string_view bytes, std::string& error);
  bool load(std::shared_ptr<const posix::MappedFile> file, std::string& error);
  bool next(const int16_t*& samples, std::size_t& count, uint8_t& probability);
  uint64_t position() const { return position_; }
  void reset();
  const WakeModel* model() const { return model_.get(); }

 private:
  bool use(std::unique_ptr<WakeModel> model);

  std::unique_ptr<WakeModel> model_;
  WakeFrontend frontend_;
  std::array<int8_t, 128> quantized_{};
  std::size_t frame_ = 0;
  uint64_t position_ = 0;
};

}
