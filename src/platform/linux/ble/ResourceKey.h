#pragma once

#include <cstdint>
#include <string>

namespace awtrix::ble {

struct ResourceKey {
  std::string script;
  uint32_t id = 0;
  bool operator<(const ResourceKey& other) const {
    return script != other.script ? script < other.script : id < other.id;
  }
};

}  // namespace awtrix::ble
