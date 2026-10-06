#pragma once

#include <functional>
#include <string>

namespace awtrix {
class PlatformMqttCommands {
 protected:
  std::function<bool(const std::string&, const std::string&, std::string&)> platformCommands_;
  bool platformMqttCommand(const std::string& topic, const std::string& body, std::string& result);
};
}
