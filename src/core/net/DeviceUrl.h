#pragma once

#include <string>
#include <utility>

namespace awtrix::net {

inline std::string deviceAddress(std::string ip, int port) {
  if (port > 0 && port != 80) ip += ":" + std::to_string(port);
  return ip;
}

inline std::string deviceUrl(std::string ip, int port) {
  return "http://" + deviceAddress(std::move(ip), port);
}

}
