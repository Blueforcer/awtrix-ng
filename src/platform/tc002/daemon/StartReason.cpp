#include "platform/tc002/daemon/StartReason.h"

#include <fcntl.h>
#include <unistd.h>

#include "platform/posix/Files.h"
#include "platform/tc002/contract/RuntimeContract.h"

namespace awtrix {
namespace tc002d {

std::string firstStartReason(const DaemonOptions& options) {
  const bool restarted = ::access(options.startedPath().c_str(), F_OK) == 0;
  posix::UniqueFd started(
      ::open(options.startedPath().c_str(), O_WRONLY | O_CREAT | O_CLOEXEC | O_NOFOLLOW, 0600));
  if (restarted) return tc002::kStartSoftware;
  if (::unlink(options.rebootMarkerPath().c_str()) == 0) {
    posix::fsyncDirectory(posix::parentDirectory(options.rebootMarkerPath()));
    return tc002::kStartSoftware;
  }
  return tc002::kStartPowerOn;
}

void markReboot(const DaemonOptions& options) {
  posix::UniqueFd marker(
      ::open(options.rebootMarkerPath().c_str(), O_WRONLY | O_CREAT | O_TRUNC | O_CLOEXEC | O_NOFOLLOW, 0600));
  if (marker.valid()) ::fsync(marker.get());
}

}
}
