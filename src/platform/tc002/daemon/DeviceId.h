#pragma once

#include <string>
#include <string_view>

// The clock's id handed to the runtime as --uid (RuntimeContract.h): the Wi-Fi MAC as twelve
// lowercase hex digits, as mDNS announces it. The Wi-Fi driver reports the MAC seconds after the
// daemon started, so the id is kept in a file for the runtime starts that come before it.
namespace awtrix {
namespace tc002d {

class DeviceId {
 public:
  // An empty path keeps the id in memory only.
  explicit DeviceId(std::string path);

  // Empty until a MAC was seen, in this boot or an earlier one.
  const std::string& value() const { return value_; }
  // Takes the id of a MAC as the Wi-Fi service publishes it; true when the id changed. An id
  // the file cannot take still holds until the daemon ends.
  bool observe(std::string_view mac);

 private:
  std::string path_;
  std::string value_;
};

}
}
