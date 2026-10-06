#include "platform/posix/Files.h"
#include "platform/tc002/daemon/ip/SystemPlatform.h"

#include <cerrno>
#include <csignal>
#include <cstring>
#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>

#include "platform/tc002/daemon/Log.h"

namespace awtrix {
namespace tc002d {
namespace ip {
namespace {

int accessPointDirectory(const std::string& directory, bool create) {
  if (directory.empty() || directory.front() != '/' || directory.size() > 512) return -1;
  std::size_t component = 1;
  for (std::size_t at = 1; at <= directory.size(); ++at) {
    if (at == directory.size() || directory[at] == '/') {
      const std::string part = directory.substr(component, at - component);
      if (part.empty() || part == "." || part == "..") return -1;
      component = at + 1;
    } else {
      const char c = directory[at];
      if (!((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') ||
            c == '.' || c == '-' || c == '_')) return -1;
    }
  }
  posix::UniqueFd parent(::open(directory.c_str(), O_RDONLY | O_DIRECTORY | O_CLOEXEC | O_NOFOLLOW));
  if (!parent.valid()) return -1;
  struct stat info{};
  if (::fstat(parent.get(), &info) != 0 || info.st_uid != ::geteuid() || (info.st_mode & 0022)) return -1;
  return posix::openPrivateDirectoryAt(parent.get(), "access-point", create).release();
}

bool privateFile(int directory, const char* name, const std::string& contents) {
  const int fd = ::openat(directory, name, O_WRONLY | O_CREAT | O_CLOEXEC | O_NOFOLLOW | O_NONBLOCK, 0600);
  if (fd < 0) return false;
  struct stat info{};
  bool ok = ::fstat(fd, &info) == 0 && S_ISREG(info.st_mode) && info.st_nlink == 1 &&
            info.st_uid == ::geteuid() && ::fchmod(fd, 0600) == 0 && ::ftruncate(fd, 0) == 0;
  if (ok) ok = posix::writeAll(fd, contents.data(), contents.size());
  if (::close(fd) != 0) ok = false;
  return ok;
}

}

SystemPlatform::SystemPlatform(const std::string& runDir) : resolver_(runDir + "/resolv.conf") {}

pid_t SystemPlatform::spawn(const SpawnRequest& request, std::string& error) { return spawnChild(request, error); }

void SystemPlatform::signal(pid_t pid, int number, bool wholeGroup) {
  if (pid > 0) ::kill(wholeGroup ? -pid : pid, number);
}

bool SystemPlatform::setKernelHostname(const std::string& hostname) {
  return ::sethostname(hostname.data(), hostname.size()) == 0;
}

std::string SystemPlatform::interfaceMac(const std::string& interfaceName) {
  std::string out;
  return posix::readText("/sys/class/net/" + interfaceName + "/address", out) ? out : std::string();
}

bool SystemPlatform::executable(const std::string& path) { return ::access(path.c_str(), X_OK) == 0; }

bool SystemPlatform::prepareDirectory(const std::string& path) {
  return posix::makeDirectories(path, 0755);
}

bool SystemPlatform::prepareAccessPoint(const std::string& directory, const std::string& config, std::string& error) {
  const int fd = accessPointDirectory(directory, true);
  if (fd < 0) {
    error = "cannot open private access-point directory";
    return false;
  }
  const bool ok = config.size() <= 4096 && privateFile(fd, "leases", "") && privateFile(fd, "udhcpd.conf", config);
  if (!ok) error = std::string("access-point files: ") + std::strerror(errno);
  ::close(fd);
  return ok;
}

bool SystemPlatform::removeFile(const std::string& path) {
  if (::unlink(path.c_str()) != 0 && errno != ENOENT) return false;
  return posix::fsyncDirectory(posix::parentDirectory(path));
}

void SystemPlatform::removeAccessPoint(const std::string& directory) {
  const int fd = accessPointDirectory(directory, false);
  if (fd < 0) return;
  ::unlinkat(fd, "udhcpd.conf", 0);
  ::unlinkat(fd, "leases", 0);
  ::close(fd);
}

int64_t SystemPlatform::monotonicMs() { return clock_.monotonicNs() / 1000000; }

void SystemPlatform::log(const char* component, const std::string& line) { Log::text(component, line); }

}
}
}
