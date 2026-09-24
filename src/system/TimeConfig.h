#pragma once
#include <string>

namespace awtrix {
// Commit before invoking the platform adapter: server().c_str() remains owned
// for SNTP implementations that retain the hostname rather than copying it.
class TimeConfig {
 public:
  bool needsUpdate(const std::string& tz, const std::string& server, bool force = false) const {
    return !applied_ || force || tz != tz_ || server != server_;
  }
  bool update(const std::string& tz, const std::string& server, bool force = false) {
    if (!needsUpdate(tz, server, force)) return false;
    tz_ = tz;
    server_ = server;
    applied_ = true;
    return true;
  }
  const std::string& timezone() const { return tz_; }
  const std::string& server() const { return server_; }
 private:
  bool applied_ = false;
  std::string tz_, server_;
};
}
