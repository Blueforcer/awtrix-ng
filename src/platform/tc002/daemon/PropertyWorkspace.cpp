#include "platform/tc002/daemon/PropertyWorkspace.h"

#include <fcntl.h>
#include <sys/stat.h>

#include <csignal>
#include <cstdio>
#include <cstdlib>

namespace awtrix {
namespace tc002d {
namespace {

struct Workspace {
  int fd = -1;
  std::string environment;
};

Workspace& workspace() {
  static Workspace instance;
  return instance;
}

}

bool PropertyWorkspace::adopt() {
  auto& w = workspace();
  w = Workspace{};
  const char* value = std::getenv("ANDROID_PROPERTY_WORKSPACE");
  int fd = -1;
  unsigned long size = 0;
  char trailing = 0;
  struct stat info{};
  if (!value || std::sscanf(value, "%d,%lu%c", &fd, &size, &trailing) != 2 || fd < 3 || fd >= 1024 || size == 0 ||
      ::fstat(fd, &info) != 0 || !S_ISREG(info.st_mode) || info.st_size < 0 ||
      static_cast<unsigned long>(info.st_size) < size || (::fcntl(fd, F_GETFL) & O_ACCMODE) != O_RDONLY ||
      ::fcntl(fd, F_SETFD, FD_CLOEXEC) < 0)
    return false;
  w.fd = fd;
  w.environment = std::string("ANDROID_PROPERTY_WORKSPACE=") + value;
  return true;
}

int PropertyWorkspace::fd() { return workspace().fd; }

const std::string& PropertyWorkspace::environment() { return workspace().environment; }

bool PropertyWorkspace::setprop(const std::string& program, const std::string& key, const std::string& value,
                                ProcessSpec& out) {
  const Workspace& w = workspace();
  if (w.fd < 0) return false;
  out = ProcessSpec{};
  out.path = program;
  out.argv = {"setprop", key, value};
  out.environment = {"PATH=/bin:/sbin", w.environment};
  out.descriptors = {{w.fd, w.fd}};
  out.parentDeathSignal = SIGTERM;
  out.umask = 077;
  return true;
}

}
}
