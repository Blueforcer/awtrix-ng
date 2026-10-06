#pragma once

#include <string>

namespace awtrix {
class PlatformMqttCommands {
 protected:
  static bool platformMqttCommand(const std::string&, const std::string&, std::string&) {
    return false;
  }
};
}
