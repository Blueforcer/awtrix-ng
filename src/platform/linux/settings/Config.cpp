#include "platform_settings/Config.h"
#include "core/ConfigRules.h"
#include "core/Sha256Hex.h"
#include "persistence/DeviceConfigRows.h"

namespace awtrix {
bool PlatformConfig::validate(cfgrules::ConfigError& err) const {
  if (!mqttTlsPin.empty() && !isSha256Hex(mqttTlsPin)) {
    err = {"mqttTlsPin", "expected 64 lowercase hex digits"};
    return false;
  }
  return true;
}

bool PlatformConfig::offers(std::string_view key) const {
  for (const auto& row : configfields::kRows)
    if (key == row.key) return offers(row.need);
  return true;
}
}
