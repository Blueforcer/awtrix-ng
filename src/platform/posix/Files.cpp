#include "platform/posix/Files.h"

#include <fcntl.h>
#include <sys/stat.h>

#include <cerrno>
#include <cstdio>
#include <cstdlib>
#include <ctime>

namespace awtrix {
namespace posix {

bool readAll(int fd, void* data, std::size_t size) {
  auto* bytes = static_cast<unsigned char*>(data);
  while (size) {
    const ssize_t count = ::read(fd, bytes, size);
    if (count < 0 && errno == EINTR) continue;
    if (count <= 0) {
      if (count == 0) errno = EIO;
      return false;
    }
    bytes += count;
    size -= static_cast<std::size_t>(count);
  }
  return true;
}

bool preadAll(int fd, uint64_t offset, void* data, std::size_t size) {
  auto* bytes = static_cast<unsigned char*>(data);
  while (size) {
    const ssize_t count = ::pread(fd, bytes, size, static_cast<off_t>(offset));
    if (count < 0 && errno == EINTR) continue;
    if (count <= 0) {
      if (count == 0) errno = EIO;
      return false;
    }
    bytes += count;
    offset += static_cast<uint64_t>(count);
    size -= static_cast<std::size_t>(count);
  }
  return true;
}

bool writeAll(int fd, const void* data, std::size_t size) {
  const auto* bytes = static_cast<const unsigned char*>(data);
  while (size) {
    const ssize_t count = ::write(fd, bytes, size);
    if (count < 0 && errno == EINTR) continue;
    if (count <= 0) {
      if (count == 0) errno = EIO;
      return false;
    }
    bytes += count;
    size -= static_cast<std::size_t>(count);
  }
  return true;
}

bool readText(const std::string& path, std::string& out, std::size_t maximum) {
  UniqueFd fd(::open(path.c_str(), O_RDONLY | O_CLOEXEC | O_NOCTTY));
  if (!fd.valid()) return false;
  out.clear();
  char buffer[512];
  for (;;) {
    const ssize_t count = ::read(fd.get(), buffer, sizeof buffer);
    if (count < 0 && errno == EINTR) continue;
    if (count < 0) return false;
    if (count == 0) return true;
    if (out.size() + static_cast<std::size_t>(count) > maximum) {
      errno = EFBIG;
      return false;
    }
    out.append(buffer, static_cast<std::size_t>(count));
  }
}

bool writeText(const std::string& path, std::string_view text) {
  UniqueFd fd(::open(path.c_str(), O_WRONLY | O_TRUNC | O_CLOEXEC | O_NOCTTY));
  if (!fd.valid()) return false;
  ssize_t count;
  do { count = ::write(fd.get(), text.data(), text.size()); } while (count < 0 && errno == EINTR);
  if (count == static_cast<ssize_t>(text.size())) return true;
  if (count >= 0) errno = EIO;
  return false;
}

bool readRegularFile(int directory, const std::string& path, std::size_t maximum, std::string& out) {
  UniqueFd fd(::openat(directory, path.c_str(), O_RDONLY | O_NOFOLLOW | O_CLOEXEC | O_NONBLOCK));
  if (!fd.valid()) return false;
  struct stat info{};
  if (::fstat(fd.get(), &info) < 0) return false;
  if (!S_ISREG(info.st_mode) || info.st_size < 0 || uint64_t(info.st_size) > maximum) {
    errno = EINVAL;
    return false;
  }
  std::string value(static_cast<std::size_t>(info.st_size), '\0');
  if (!readAll(fd.get(), value.data(), value.size())) return false;
  // Reject growth between stat and read rather than silently returning a prefix.
  char extra;
  ssize_t count;
  do { count = ::read(fd.get(), &extra, 1); } while (count < 0 && errno == EINTR);
  if (count != 0) {
    if (count > 0) errno = EFBIG;
    return false;
  }
  out = std::move(value);
  return true;
}

bool replaceText(const std::string& path, std::string_view text) {
  std::string temporary = path + ".tmp.XXXXXX";
  const int fd = ::mkostemp(temporary.data(), O_CLOEXEC);
  if (fd < 0) return false;
  bool okay = ::fchmod(fd, 0600) == 0 && writeAll(fd, text.data(), text.size()) && ::fsync(fd) == 0;
  int error = okay ? 0 : errno;
  if (::close(fd) != 0 && okay) { okay = false; error = errno; }
  if (!okay) errno = error;
  if (okay) okay = ::rename(temporary.c_str(), path.c_str()) == 0;
  if (!okay) {
    const int error = errno;
    ::unlink(temporary.c_str());
    errno = error;
    return false;
  }
  return fsyncDirectory(parentDirectory(path));
}

std::string trimmed(std::string_view text) {
  const auto blank = [](char c) { return c == ' ' || c == '\t' || c == '\r' || c == '\n' || c == '\0'; };
  std::size_t begin = 0, end = text.size();
  while (begin < end && blank(text[begin])) ++begin;
  while (end > begin && blank(text[end - 1])) --end;
  return std::string(text.substr(begin, end - begin));
}

std::string hexBytes(const void* data, std::size_t size) {
  constexpr char digits[] = "0123456789abcdef";
  const auto* bytes = static_cast<const unsigned char*>(data);
  std::string out;
  out.reserve(size * 2);
  for (std::size_t i = 0; i < size; ++i) {
    out += digits[bytes[i] >> 4];
    out += digits[bytes[i] & 15];
  }
  return out;
}

bool numeric(const char* text) {
  if (!text || !*text) return false;
  for (; *text; ++text)
    if (*text < '0' || *text > '9') return false;
  return true;
}

bool ensurePrivateDirectory(const std::string& path) {
  return openPrivateDirectory(path, true).valid();
}

UniqueFd openPrivateDirectoryAt(int directory, const std::string& path, bool create, bool repair) {
  if (create && ::mkdirat(directory, path.c_str(), 0700) < 0 && errno != EEXIST) return {};
  UniqueFd fd(::openat(directory, path.c_str(), O_RDONLY | O_DIRECTORY | O_CLOEXEC | O_NOFOLLOW));
  if (!fd.valid()) return {};
  struct stat info{};
  if (::fstat(fd.get(), &info) < 0) return {};
  if (!S_ISDIR(info.st_mode) || info.st_uid != ::geteuid()) {
    errno = EPERM;
    return {};
  }
  if ((info.st_mode & 07777) != 0700) {
    if (!repair) { errno = EPERM; return {}; }
    if (::fchmod(fd.get(), 0700) < 0) return {};
  }
  return fd;
}

UniqueFd openPrivateDirectory(const std::string& path, bool create) {
  return openPrivateDirectoryAt(AT_FDCWD, path, create, create);
}

bool makeDirectories(const std::string& path, unsigned mode) {
  if (path.empty()) {
    errno = EINVAL;
    return false;
  }
  std::size_t at = 1;
  for (;;) {
    at = path.find('/', at);
    const std::string prefix = path.substr(0, at);
    if (::mkdir(prefix.c_str(), mode) < 0 && errno != EEXIST) return false;
    if (at == std::string::npos) break;
    ++at;
  }
  struct stat info{};
  if (::stat(path.c_str(), &info) < 0) return false;
  if (!S_ISDIR(info.st_mode)) {
    errno = ENOTDIR;
    return false;
  }
  return true;
}

bool fsyncDirectory(const std::string& path) {
  UniqueFd fd(::open(path.c_str(), O_RDONLY | O_DIRECTORY | O_CLOEXEC));
  return fd.valid() && ::fsync(fd.get()) == 0;
}

std::string parentDirectory(const std::string& path) {
  const auto slash = path.rfind('/');
  if (slash == std::string::npos) return ".";
  if (slash == 0) return "/";
  return path.substr(0, slash);
}

}
}
