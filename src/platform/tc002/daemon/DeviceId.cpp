#include "platform/tc002/daemon/DeviceId.h"

#include <fcntl.h>
#include <unistd.h>

#include <cerrno>
#include <cstring>
#include <utility>

#include "platform/posix/Files.h"
#include "platform/tc002/contract/RuntimeContract.h"
#include "platform/tc002/daemon/Log.h"
#include "platform/tc002/daemon/ip/Hostname.h"

namespace awtrix {
namespace tc002d {

DeviceId::DeviceId(std::string path) : path_(std::move(path)) {
  if (path_.empty()) return;
  std::string stored;
  if (!posix::readText(path_, stored, 64)) {
    if (errno != ENOENT)
      Log::line("daemon", "cannot read the device id in %s: %s", path_.c_str(), std::strerror(errno));
    return;
  }
  stored = posix::trimmed(stored);
  if (tc002::validUid(stored)) value_ = stored;
  else Log::line("daemon", "ignoring the malformed device id in %s", path_.c_str());
}

bool DeviceId::observe(std::string_view mac) {
  const std::string id = ip::macId(mac);
  if (id.empty() || id == value_) return false;
  value_ = id;
  if (path_.empty() || posix::replaceText(path_, id + "\n")) Log::line("daemon", "device id %s from the Wi-Fi MAC", id.c_str());
  else
    Log::line("daemon", "device id %s from the Wi-Fi MAC, not kept in %s: %s", id.c_str(), path_.c_str(),
              std::strerror(errno));
  return true;
}

}
}
