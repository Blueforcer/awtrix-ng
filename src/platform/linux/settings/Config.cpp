#include "platform_settings/Config.h"
#include "core/ConfigRules.h"
#include "persistence/DeviceConfigRows.h"

namespace awtrix {
bool PlatformConfig::validate(cfgrules::ConfigError&) const {
  return true;
}

bool PlatformConfig::offers(std::string_view key) const {
  for (const auto& row : configfields::kRows)
    if (key == row.key) return offers(row.need);
  return true;
}
}
