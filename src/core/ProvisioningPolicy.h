#pragma once

#include <string>

namespace awtrix {
namespace provisioning {

// While the device is its own access point anyone nearby can reach the API, so writes are cut down
// to what setup actually needs: store the WiFi details, then reboot into them.
inline bool apModeAllows(const std::string& method, const std::string& path) {
  if (method == "GET") return true;
  if (method == "PUT") return path == "/api/v1/system";
  return method == "POST" && path == "/api/v1/device/reboot";
}

}
}
