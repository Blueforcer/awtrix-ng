#pragma once

#include <cstdint>
#include <string_view>

#include "core/render/Canvas.h"

namespace awtrix {

class Fingerprint {
 public:
  void add(uint32_t value);
  void add(std::string_view text);
  void add(const Canvas& canvas);
  uint32_t value() const { return hash_; }

 private:
  uint32_t hash_ = 2166136261u;
};

}
