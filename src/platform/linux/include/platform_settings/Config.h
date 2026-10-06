#pragma once

#include <string>
#include <string_view>
#include "persistence/ConfigNeed.h"

namespace awtrix {
namespace cfgrules { struct ConfigError; }
struct PlatformConfig {
#define PLATFORM_CONFIG(type, member, initial, key, secret, need) type member = initial;
#include "platform_settings/ConfigFields.inc"
#undef PLATFORM_CONFIG
  bool panelConfigurable = true;
  bool gpioConfigurable = true;
  constexpr bool offers(ConfigNeed need) const {
    return need == ConfigNeed::None ||
           (need == ConfigNeed::Panel ? panelConfigurable : gpioConfigurable);
  }
  bool offers(std::string_view key) const;
  bool validate(cfgrules::ConfigError& error) const;
  using Ignore = bool (*)(std::string_view, const void*);
  static constexpr Ignore ignoredFields() { return ignore; }
 private:
  static bool ignore(std::string_view key, const void* context) {
    return !static_cast<const PlatformConfig*>(context)->offers(key);
  }
};
}
