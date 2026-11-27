#include "platform/host/app/macos/DefaultsChoice.h"

namespace awtrix::hostapp {

std::optional<bool> DefaultsChoice::stored() const {
  id value = [defaults_ objectForKey:key_];
  if (![value isKindOfClass:NSNumber.class]) return std::nullopt;
  return [value boolValue];
}

void DefaultsChoice::store(bool on) { [defaults_ setBool:on forKey:key_]; }

}
