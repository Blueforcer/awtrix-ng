#pragma once

#include <cstddef>
#include "core/mqtt/Entity.h"

namespace awtrix::ha {
struct PlatformEntities {
  const Entity* platformEntities = nullptr;
  std::size_t platformEntityCount = 0;
  void setPlatformEntities(const Entity* entities, std::size_t count) {
    platformEntities = entities;
    platformEntityCount = count;
  }
  template <typename Emit>
  void eachPlatformEntity(Emit emit) const {
    for (std::size_t i = 0; i < platformEntityCount; ++i) emit(platformEntities[i]);
  }
};
}
