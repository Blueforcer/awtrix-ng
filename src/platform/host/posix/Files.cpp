#include "platform/host/Files.h"

#include <fcntl.h>
#include <sys/file.h>
#include <sys/stat.h>
#include <unistd.h>

#include <cerrno>
#include <utility>

#include "platform/posix/Files.h"

namespace awtrix::os {

bool readFile(const std::string& path, std::string& out, std::size_t maximum) {
  return posix::readText(path, out, maximum);
}

bool replaceFile(const std::string& path, std::string_view text) { return posix::replaceText(path, text); }

bool syncDirectory(const std::string& path) { return posix::fsyncDirectory(path); }

bool ensurePrivateDirectory(const std::string& path) { return posix::ensurePrivateDirectory(path); }

bool removeFile(const std::string& path) { return ::unlink(path.c_str()) == 0; }

void keepNewFilesPrivate() { ::umask(0077); }

int openForReading(const std::string& path) { return ::open(path.c_str(), O_RDONLY | O_CLOEXEC | O_NOFOLLOW); }

OutFile::~OutFile() { close(); }

OutFile::OutFile(OutFile&& other) noexcept
    : handle_(std::exchange(other.handle_, -1)), exists_(other.exists_), full_(other.full_) {}

OutFile& OutFile::operator=(OutFile&& other) noexcept {
  if (this != &other) {
    close();
    handle_ = std::exchange(other.handle_, -1);
    exists_ = other.exists_;
    full_ = other.full_;
  }
  return *this;
}

OutFile OutFile::create(const std::string& path, bool exclusive) {
  OutFile file;
  const int flags = O_WRONLY | O_CREAT | O_CLOEXEC | O_NOFOLLOW | (exclusive ? O_EXCL : O_TRUNC);
  file.handle_ = ::open(path.c_str(), flags, 0600);
  if (file.handle_ < 0) {
    file.exists_ = errno == EEXIST;
    file.full_ = errno == ENOSPC;
  }
  return file;
}

bool OutFile::write(const void* data, std::size_t size) {
  if (handle_ < 0) return false;
  if (posix::writeAll(static_cast<int>(handle_), data, size)) return true;
  full_ = errno == ENOSPC;
  return false;
}

bool OutFile::sync() { return handle_ >= 0 && ::fsync(static_cast<int>(handle_)) == 0; }

bool OutFile::close() {
  if (handle_ < 0) return false;
  const bool closed = ::close(static_cast<int>(std::exchange(handle_, -1))) == 0;
  if (!closed) full_ = errno == ENOSPC;
  return closed;
}

FileLock::~FileLock() {
  if (handle_ >= 0) ::close(static_cast<int>(handle_));
}

bool FileLock::acquire(const std::string& path) {
  const int fd = ::open(path.c_str(), O_RDWR | O_CREAT | O_CLOEXEC, 0600);
  if (fd < 0) return false;
  if (::flock(fd, LOCK_EX | LOCK_NB) != 0) {
    ::close(fd);
    return false;
  }
  if (handle_ >= 0) ::close(static_cast<int>(handle_));
  handle_ = fd;
  return true;
}

}
