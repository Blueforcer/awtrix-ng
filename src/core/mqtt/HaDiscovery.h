#pragma once

#include <string>

#include "core/mqtt/ByteSink.h"

namespace awtrix {
namespace ha {

struct DiscoveryContext {
  std::string prefix;
  std::string haPrefix = "homeassistant";
  std::string uid;
  std::string hostname;
  std::string version;
  bool hasBattery = false;
  bool hasLightSensor = false;
  bool hasTemperature = false;
  bool hasHumidity = false;
  bool hasPressure = false;
};

std::string discoveryTopic(const DiscoveryContext& ctx);

void emit(const DiscoveryContext& ctx, IByteSink& sink);

}
}
