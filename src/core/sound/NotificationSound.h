#pragma once

#include <string>

#include "core/payload/AppSpec.h"
#include "core/sound/AudioRouter.h"

namespace awtrix {
namespace sound {

struct Request {
  bool present = false;
  Source source = Source::Auto;
  std::string value;
};

// Spelled-out RTTTL is the sender's deliberate choice and wins over a stored name.
inline Request requestForSpec(const AppSpec& spec) {
  const std::string& rtttl = spec.extras().rtttl;
  if (!rtttl.empty()) return {true, Source::Rtttl, rtttl};
  if (!spec.sound.empty()) return {true, Source::Auto, spec.sound};
  return {};
}

}
}
