#pragma once

#include <cstdint>
#include <string>

namespace awtrix {
namespace mirror {

enum class FollowState : uint8_t {
  Off,
  Offline,
  Resolving,
  NotFound,
  Waiting,
  Idle,
  Filtered,
  SizeMismatch,
  NoMemory,
  Showing
};

// These names go into the API state JSON, so they are part of the public contract.
inline const char* followStateName(FollowState state) {
  switch (state) {
    case FollowState::Off:          return "off";
    case FollowState::Offline:      return "offline";
    case FollowState::Resolving:    return "resolving";
    case FollowState::NotFound:     return "notFound";
    case FollowState::Waiting:      return "waiting";
    case FollowState::Idle:         return "idle";
    case FollowState::Filtered:     return "filtered";
    case FollowState::SizeMismatch: return "sizeMismatch";
    case FollowState::NoMemory:     return "noMemory";
    case FollowState::Showing:      return "showing";
  }
  return "off";
}

struct Status {
  bool sharing = false;
  uint8_t viewers = 0;
  FollowState follow = FollowState::Off;
  std::string source;
  // The panel size the followed clock last reported, 0 until it has answered.
  uint16_t sourceWidth = 0;
  uint16_t sourceHeight = 0;
};

}
}
