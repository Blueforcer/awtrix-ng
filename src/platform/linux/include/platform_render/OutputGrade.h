#pragma once

#include <vector>
#include "core/render/OutputTable.h"

namespace awtrix::render {
class OutputGrade {
 protected:
  void prepareOutput();
  void rememberOutput(int channel, int level, uint16_t light) {
    if (output_) full_[channel * 256 + level] = light;
  }
  uint8_t encodeOutput(uint32_t light) const;
  void settleOutput(uint8_t brightness, uint8_t r, uint8_t g, uint8_t b, uint8_t* codes) const {
    if (output_ && !(codes[0] && codes[1] && codes[2])) settleFloor(brightness, r, g, b, codes);
  }
  const OutputTable* output_ = nullptr;

 private:
  void settleFloor(uint8_t brightness, uint8_t r, uint8_t g, uint8_t b, uint8_t* codes) const;
  std::vector<uint16_t> full_;
  uint8_t floorCode_ = 0;
};
}
