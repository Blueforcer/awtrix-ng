#include "platform/tc002/update/FileStateStore.h"
#include "platform/tc002/update/StateDocument.h"
#include "platform/posix/Files.h"

#include <cerrno>
#include <cstring>
#include <fcntl.h>
#include <sys/file.h>
#include <sys/stat.h>
#include <unistd.h>
#include <utility>

namespace awtrix::tc002::update {
namespace {
constexpr char kStateFile[] = "update-state.json";
constexpr char kTemporaryFile[] = ".update-state.tmp";

std::string describeErrno(const char* what) { return std::string(what) + ": " + std::strerror(errno); }

}  // namespace

// O_NOFOLLOW protects only the last path component, so the name the caller
// gives must be a real one: trailing slashes are dropped and a final "." or
// ".." is refused, otherwise a symlink named one level up would be followed.
FileStateStore::FileStateStore(const std::string& directory, Fault fault) : fault_(std::move(fault)) {
  std::string path = directory;
  while (path.size() > 1 && path.back() == '/') path.pop_back();
  const auto slash = path.rfind('/');
  const std::string_view last = slash == std::string::npos ? std::string_view(path)
                                                           : std::string_view(path).substr(slash + 1);
  if (path.find('\0') != std::string::npos || last.empty() || last == "." || last == "..") {
    error_ = "invalid state directory path";
    return;
  }
  auto opened = posix::openPrivateDirectoryAt(AT_FDCWD, path, false);
  if (!opened.valid()) {
    error_ = errno == EPERM ? "state directory must be caller-owned with mode 0700"
                           : describeErrno("cannot open state directory");
    return;
  }
  const int fd = opened.release();
  if (::flock(fd, LOCK_EX | LOCK_NB) != 0) {
    error_ = errno == EWOULDBLOCK ? "state directory is in use" : describeErrno("cannot lock state directory");
    ::close(fd);
    return;
  }
  directory_ = fd;
}

FileStateStore::~FileStateStore() {
  if (directory_ < 0) return;
  ::flock(directory_, LOCK_UN);
  ::close(directory_);
}

bool FileStateStore::read(std::string& json, bool& exists, std::string& error) {
  json.clear();
  exists = false;
  if (!ok()) { error = "state directory is not open"; return false; }
  if (faulted("read")) { errno = EIO; error = describeErrno("cannot read state file"); return false; }
  const int fd = ::openat(directory_, kStateFile, O_RDONLY | O_NOFOLLOW | O_CLOEXEC | O_NONBLOCK);
  if (fd < 0) {
    if (errno == ENOENT) return true;
    error = describeErrno("cannot open state file");
    return false;
  }
  struct stat status {};
  bool accepted = false;
  if (::fstat(fd, &status) != 0) error = describeErrno("cannot inspect state file");
  else if (!S_ISREG(status.st_mode)) error = "state file must be a regular file";
  else if (status.st_uid != ::geteuid() || (status.st_mode & 077) != 0)
    error = "state file must be caller-owned and private";
  else if (status.st_size < 0 || static_cast<std::uint64_t>(status.st_size) > kMaxStateBytes)
    error = "state file exceeds the size limit";
  else {
    json.resize(static_cast<std::size_t>(status.st_size));
    char extra = 0;
    ssize_t eof;
    if (!posix::readAll(fd, json.data(), json.size())) error = describeErrno("cannot read state file");
    else {
      do { eof = ::read(fd, &extra, 1); } while (eof < 0 && errno == EINTR);
      if (eof != 0) error = "state file changed while reading";
      else accepted = true;
    }
  }
  ::close(fd);
  if (!accepted) { json.clear(); return false; }
  exists = true;
  return true;
}

// unlink-stale, open, write, fsync-file, close, rename, fsync-directory. A
// failure before rename removes the temporary file and leaves the previous
// document untouched; a failure at the directory sync leaves the new document
// published with uncertain durability, which the error text names.
bool FileStateStore::write(const std::string& json, std::string& error) {
  if (!ok()) { error = "state directory is not open"; return false; }
  const auto injected = [&](const char* step) {
    if (!faulted(step)) return false;
    errno = EIO;
    return true;
  };
  if (injected("unlink-stale") || (::unlinkat(directory_, kTemporaryFile, 0) != 0 && errno != ENOENT)) {
    error = describeErrno("cannot remove a stale temporary state file");
    return false;
  }
  int fd = -1;
  if (injected("open") ||
      (fd = ::openat(directory_, kTemporaryFile, O_WRONLY | O_CREAT | O_EXCL | O_NOFOLLOW | O_CLOEXEC, 0600)) < 0) {
    error = describeErrno("cannot create temporary state file");
    return false;
  }
  const auto abandon = [&](const char* what) {
    error = describeErrno(what);
    if (fd >= 0) ::close(fd);
    ::unlinkat(directory_, kTemporaryFile, 0);
    return false;
  };
  if (injected("write") || !posix::writeAll(fd, json.data(), json.size()))
    return abandon("cannot write temporary state file");
  if (injected("fsync-file") || ::fsync(fd) != 0) return abandon("cannot sync temporary state file");
  const int closing = fd;
  fd = -1;
  const bool closeFault = injected("close");
  if (::close(closing) != 0 || closeFault) {
    if (closeFault) errno = EIO;
    return abandon("cannot close temporary state file");
  }
  if (injected("rename") || ::renameat(directory_, kTemporaryFile, directory_, kStateFile) != 0)
    return abandon("cannot publish state file");
  if (injected("fsync-directory") || ::fsync(directory_) != 0) {
    error = describeErrno("state file published but durability is uncertain");
    return false;
  }
  return true;
}


}  // namespace awtrix::tc002::update
