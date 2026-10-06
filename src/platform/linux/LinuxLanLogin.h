#pragma once

#include <array>
#include <mutex>
#include <string>

namespace httplib { struct Request; struct Response; }

namespace awtrix {
struct DeviceConfig;

// The device login of the LAN service, as on ESP32: while the configuration requires it, every
// request except a CORS preflight must carry the configured HTTP Basic credentials. update()
// runs on the main loop; admit() runs on listener threads before any request body is read.
class LinuxLanLogin {
 public:
  void update(const DeviceConfig& config);
  bool admit(const httplib::Request& request, httplib::Response& response) const;

 private:
  mutable std::mutex mutex_;
  bool required_ = false;
  std::array<unsigned char, 32> expected_{};
};

}
