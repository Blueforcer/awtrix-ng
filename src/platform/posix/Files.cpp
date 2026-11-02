#include "platform/posix/Files.h"

#include <dirent.h>
#include <fcntl.h>
#include <sys/file.h>
#include <sys/mman.h>
#include <sys/stat.h>

#include <cerrno>
#include <cstdio>
#include <cstdlib>
#include <ctime>
#include <utility>

namespace awtrix {
namespace posix {

namespace {

template <class Byte, class Transfer>
bool transferAll(Byte* bytes, std::size_t size, Transfer transfer) {
  for (std::size_t done = 0; done < size;) {
    const ssize_t count = transfer(bytes + done, size - done, done);
    if (count < 0 && errno == EINTR) continue;
    if (count <= 0) {
      if (count == 0) errno = EIO;
      return false;
    }
    done += static_cast<std::size_t>(count);
  }
  return true;
}

}

bool readAll(int fd, void* data, std::size_t size) {
  return transferAll(static_cast<unsigned char*>(data), size,
                     [fd](unsigned char* bytes, std::size_t left, std::size_t) { return ::read(fd, bytes, left); });
}

bool preadAll(int fd, uint64_t offset, void* data, std::size_t size) {
  return transferAll(static_cast<unsigned char*>(data), size,
                     [fd, offset](unsigned char* bytes, std::size_t left, std::size_t done) {
                       return ::pread(fd, bytes, left, static_cast<off_t>(offset + done));
                     });
}

bool writeAll(int fd, const void* data, std::size_t size) {
  return transferAll(static_cast<const unsigned char*>(data), size,
                     [fd](const unsigned char* bytes, std::size_t left, std::size_t) {
                       return ::write(fd, bytes, left);
                     });
}

bool pwriteAll(int fd, uint64_t offset, const void* data, std::size_t size) {
  return transferAll(static_cast<const unsigned char*>(data), size,
                     [fd, offset](const unsigned char* bytes, std::size_t left, std::size_t done) {
                       return ::pwrite(fd, bytes, left, static_cast<off_t>(offset + done));
                     });
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

MappedFile::MappedFile(const std::string& path, std::size_t maximum) {
  const UniqueFd fd(::open(path.c_str(), O_RDONLY | O_NOFOLLOW | O_CLOEXEC | O_NONBLOCK));
  struct stat info{};
  if (!fd.valid() || ::fstat(fd.get(), &info) < 0) return;
  if (!S_ISREG(info.st_mode) || info.st_size < 0 || uint64_t(info.st_size) > maximum) {
    errno = EINVAL;
    return;
  }
  const std::size_t size = static_cast<std::size_t>(info.st_size);
  if (size > 0) {
    void* const data = ::mmap(nullptr, size, PROT_READ, MAP_PRIVATE, fd.get(), 0);
    if (data == MAP_FAILED) return;
    data_ = data;
    size_ = size;
  }
  valid_ = true;
}

MappedFile::~MappedFile() {
  if (data_) ::munmap(data_, size_);
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

namespace {

constexpr char kTemporarySuffix[] = "XXXXXX";
constexpr std::size_t kMaxPidDigits = 9;

void removeRegularFiles(UniqueFd directory) {
  DIR* listing = ::fdopendir(directory.get());
  if (!listing) return;
  directory.release();
  while (const dirent* entry = ::readdir(listing)) {
    struct stat info{};
    if (::fstatat(::dirfd(listing), entry->d_name, &info, AT_SYMLINK_NOFOLLOW) == 0 && S_ISREG(info.st_mode))
      ::unlinkat(::dirfd(listing), entry->d_name, 0);
  }
  ::closedir(listing);
}

bool temporaryName(std::string_view name, std::string_view prefix) {
  if (name.size() <= prefix.size() || name.substr(0, prefix.size()) != prefix || name[prefix.size()] != '-')
    return false;
  name.remove_prefix(prefix.size() + 1);
  const std::size_t dash = name.find('-');
  if (dash == 0 || dash > kMaxPidDigits || name.size() - dash - 1 != sizeof(kTemporarySuffix) - 1) return false;
  for (const char digit : name.substr(0, dash))
    if (digit < '0' || digit > '9') return false;
  return true;
}

bool hold(int directory) { return ::flock(directory, LOCK_EX | LOCK_NB) == 0; }

}

void removeStaleTemporaryDirectories(const std::string& parent, std::string_view prefix) {
  DIR* listing = ::opendir(parent.c_str());
  if (!listing) return;
  while (const dirent* entry = ::readdir(listing)) {
    if (!temporaryName(entry->d_name, prefix)) continue;
    UniqueFd directory = openPrivateDirectoryAt(::dirfd(listing), entry->d_name, false);
    if (!directory.valid() || !hold(directory.get())) continue;
    removeRegularFiles(std::move(directory));
    ::unlinkat(::dirfd(listing), entry->d_name, AT_REMOVEDIR);
  }
  ::closedir(listing);
}

PrivateTemporaryDirectory::PrivateTemporaryDirectory(const std::string& parent, std::string_view prefix) {
  removeStaleTemporaryDirectories(parent, prefix);
  std::string pattern =
      parent + "/" + std::string(prefix) + "-" + std::to_string(::getpid()) + "-" + kTemporarySuffix;
  if (!::mkdtemp(pattern.data())) return;
  held_ = openPrivateDirectory(pattern, false);
  if (held_.valid() && hold(held_.get())) path_ = std::move(pattern);
  else ::rmdir(pattern.c_str());
}

PrivateTemporaryDirectory::~PrivateTemporaryDirectory() {
  if (path_.empty()) return;
  if (UniqueFd directory = openPrivateDirectory(path_, false); directory.valid())
    removeRegularFiles(std::move(directory));
  ::rmdir(path_.c_str());
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
