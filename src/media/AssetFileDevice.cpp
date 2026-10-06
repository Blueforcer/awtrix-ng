
#include <unistd.h>

#include <cerrno>

#include "media/AssetFile.h"
#include "persistence/VfsFile.h"

namespace awtrix {
namespace media {

bool readAssetRange(std::string_view path, std::size_t offset, uint8_t* out,
                    std::size_t capacity, std::size_t& read, std::size_t& fileSize) {
  read = fileSize = 0;
  const int fd = fs::openRead(path);
  if (fd < 0) return false;
  const off_t length = ::lseek(fd, 0, SEEK_END);
  if (length <= 0 || offset > static_cast<std::size_t>(length) ||
      ::lseek(fd, static_cast<off_t>(offset), SEEK_SET) != static_cast<off_t>(offset)) {
    ::close(fd); return false;
  }
  fileSize = static_cast<std::size_t>(length);
  const std::size_t count = capacity < fileSize - offset ? capacity : fileSize - offset;
  const ssize_t got = ::read(fd, out, count);
  ::close(fd);
  if (got < 0) return false;
  read = static_cast<std::size_t>(got);
  return read == count;
}

bool readAsset(std::string_view path, PodBuffer<uint8_t>& out, bool* outOfMemory) {
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
