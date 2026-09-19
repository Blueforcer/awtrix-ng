
#include <unistd.h>

#include <cerrno>

#include "media/AssetFile.h"
#include "persistence/VfsFile.h"

namespace awtrix {
namespace media {

bool readAsset(const std::string& path, PodBuffer<uint8_t>& out, bool* outOfMemory) {
  if (outOfMemory) *outOfMemory = false;
  errno = 0;
  const int fd = fs::openRead(path);
  if (fd < 0) {
    if (outOfMemory && errno == ENOMEM) *outOfMemory = true;
    return false;
  }
  const off_t n = ::lseek(fd, 0, SEEK_END);
  if (n <= 0 || ::lseek(fd, 0, SEEK_SET) != 0) {
    ::close(fd);
    return false;
  }
  if (!out.resize(static_cast<size_t>(n))) {
    if (outOfMemory) *outOfMemory = true;
    ::close(fd);
    return false;
  }
  const ssize_t got = ::read(fd, out.data(), static_cast<size_t>(n));
  ::close(fd);
  if (got != static_cast<ssize_t>(n)) {
    out.clear();
    return false;
  }
  return true;
}

}
}
