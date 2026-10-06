#include "platform/tc002/daemon/Kernel.h"

#include <fcntl.h>
#include <sys/stat.h>
#include <sys/syscall.h>
#include <unistd.h>

#include <cerrno>
#include <cstring>
#include <vector>

#include "platform/posix/Files.h"

namespace awtrix {
namespace tc002d {
namespace {

constexpr std::size_t kMaxModuleBytes = 4 * 1024 * 1024;

int initModule(int fd, const std::string& parameters) {
  if (::syscall(SYS_finit_module, fd, parameters.c_str(), 0) == 0 || errno == EEXIST) return 0;
  if (errno != ENOSYS) return errno;
  struct stat info{};
  if (::fstat(fd, &info) < 0) return errno;
  if (info.st_size <= 0 || static_cast<std::size_t>(info.st_size) > kMaxModuleBytes) return EFBIG;
  std::vector<char> image(static_cast<std::size_t>(info.st_size));
  if (!posix::preadAll(fd, 0, image.data(), image.size())) return errno ? errno : EIO;
  if (::syscall(SYS_init_module, image.data(), image.size(), parameters.c_str()) == 0 || errno == EEXIST) return 0;
  return errno;
}

}

bool trustedModuleFile(const struct stat& info, uid_t owner) {
  return S_ISREG(info.st_mode) && info.st_uid == owner && !(info.st_mode & 022);
}

int loadKernelModule(const std::string& path, const std::string& parameters, uid_t owner, const ModuleLoad& load) {
  const posix::UniqueFd fd(::open(path.c_str(), O_RDONLY | O_CLOEXEC | O_NOFOLLOW | O_NOCTTY));
  if (!fd.valid()) return errno;
  struct stat info{};
  if (::fstat(fd.get(), &info) < 0) return errno;
  if (!trustedModuleFile(info, owner)) return EPERM;
  return load ? load(fd.get(), parameters) : initModule(fd.get(), parameters);
}

std::string armPanicReboot(const std::string& sysRoot, int seconds) {
  std::string failed;
  const struct {
    const char* name;
    std::string value;
  } settings[] = {{"panic", std::to_string(seconds)}, {"panic_on_oops", "1"}};
  for (const auto& setting : settings) {
    if (posix::writeText(sysRoot + "/proc/sys/kernel/" + setting.name, setting.value + "\n")) continue;
    if (!failed.empty()) failed += ", ";
    failed += std::string(setting.name) + ": " + std::strerror(errno);
  }
  return failed;
}

}
}
