#include "persistence/VfsFile.h"

#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>

#include <cerrno>
#include <cstring>

#include "system/Log.h"

#ifndef O_BINARY
#define O_BINARY 0
#endif

namespace awtrix {
namespace fs {

namespace {
constexpr std::size_t kMaxVfsPathBytes = 256;
}

std::string vfsPath(const std::string& path) { return std::string(kMountPoint) + path; }

int openRead(const std::string& path) {
  char full[kMaxVfsPathBytes];
  const std::size_t rootLen = std::strlen(kMountPoint);
  if (rootLen + path.size() >= sizeof(full)) {
    logf("fs: a path of %u bytes exceeds the %u byte limit",
         static_cast<unsigned>(rootLen + path.size()), static_cast<unsigned>(sizeof(full) - 1));
    errno = ENAMETOOLONG;
    return -1;
  }
  std::memcpy(full, kMountPoint, rootLen);
  std::memcpy(full + rootLen, path.c_str(), path.size() + 1);
  return ::open(full, O_RDONLY | O_BINARY);
}

long fileSize(const std::string& path) {
  const int fd = openRead(path);
  if (fd >= 0) {
    const off_t n = ::lseek(fd, 0, SEEK_END);
    ::close(fd);
    return n < 0 ? -1 : static_cast<long>(n);
  }
  if (errno != ENOMEM && errno != ENFILE && errno != EMFILE) return -1;
  struct stat st;
  if (::stat(vfsPath(path).c_str(), &st) != 0 || !S_ISREG(st.st_mode)) return -1;
  return static_cast<long>(st.st_size);
}

bool isFile(const std::string& path) { return fileSize(path) >= 0; }

}
}
