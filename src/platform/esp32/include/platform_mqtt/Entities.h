#pragma once

#include <cstddef>
#include "core/mqtt/Entity.h"

namespace awtrix::ha {
struct PlatformEntities {
  void setPlatformEntities(const Entity*, std::size_t) {}
  template <typename Emit>
  void eachPlatformEntity(Emit) const {}
};
}
