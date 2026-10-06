#pragma once

#include <cstddef>
#include <string>

#include "core/mqtt/ByteSink.h"
#include "platform_mqtt/Entities.h"

namespace awtrix {
namespace ha {

struct DiscoveryContext : PlatformEntities {
  std::string prefix;
  std::string haPrefix = "homeassistant";
  std::string uid;
  std::string hostname;
  std::string version;
  std::string url;
  bool hasBattery = false;
  bool hasLightSensor = false;
  bool hasTemperature = false;
  bool hasHumidity = false;
  bool hasPressure = false;
  // Any sound output, and a radio among them: the mixer's volumes and the stop button.
  bool hasSound = false;
  bool hasRadio = false;
};

std::string discoveryTopic(const DiscoveryContext& ctx);

void emit(const DiscoveryContext& ctx, IByteSink& sink);

}
}
