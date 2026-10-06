#pragma once

#include <cstdint>
#include <string>

namespace awtrix::ble {

// Where script requests go: each names the script it acts for, so scans, links, subscriptions,
// adverts and services stay apart and forget() drops exactly one script's share. The answer is
// synchronous JSON; events follow through BleScripting::push.
class BleBackend {
 public:
  virtual ~BleBackend() = default;
  virtual std::string call(const std::string& script, const std::string& op, const std::string& args,
                           uint32_t id) = 0;
  virtual void forget(const std::string& script) = 0;
};

}
