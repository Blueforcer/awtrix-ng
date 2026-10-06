#include "platform/tc002/daemon/Kernel.h"

#include <sys/stat.h>

#include "support.h"

using namespace awtrix::tc002d;
using tc002d_test::check;
using tc002d_test::readFile;
using tc002d_test::TempDir;
using tc002d_test::writeFile;

namespace {

void panicReboot() {
  TempDir root;
  const std::string kernel = root / "proc/sys/kernel";
  check(writeFile(kernel + "/panic", "0\n") && writeFile(kernel + "/panic_on_oops", "0\n"), "sysctl files");
  check(armPanicReboot(root.path()).empty(), "both sysctls written");
  check(readFile(kernel + "/panic") == "5\n", "kernel.panic = 5");
  check(readFile(kernel + "/panic_on_oops") == "1\n", "kernel.panic_on_oops = 1");
  check(armPanicReboot(root.path(), 10).empty() && readFile(kernel + "/panic") == "10\n", "the delay is configurable");
  ::unlink((kernel + "/panic_on_oops").c_str());
  const std::string failed = armPanicReboot(root.path());
  check(failed == "panic_on_oops: No such file or directory", "a missing sysctl is reported: " + failed);
  check(readFile(kernel + "/panic") == "5\n", "the other sysctl is still written");
}

void moduleChecks() {
  TempDir root;
  const std::string module = root / "m.ko";
  check(writeFile(module, "module image") && ::chmod(module.c_str(), 0644) == 0, "module file");
  std::string seen;
  const ModuleLoad record = [&seen](int fd, const std::string& parameters) {
    struct stat info{};
    seen = ::fstat(fd, &info) == 0 && S_ISREG(info.st_mode) ? "fd " + parameters : "bad fd";
    return 0;
  };
  check(loadKernelModule(module, "debug=0", ::geteuid(), record) == 0 && seen == "fd debug=0",
        "a module of its owner loads with its parameters: " + seen);
  seen.clear();
  check(loadKernelModule(module, "", ::geteuid() + 1, record) == EPERM && seen.empty(), "a foreign owner is refused");
  check(::chmod(module.c_str(), 0664) == 0 && loadKernelModule(module, "", ::geteuid(), record) == EPERM && seen.empty(),
        "a group-writable module is refused");
  check(::chmod(module.c_str(), 0644) == 0 && ::symlink(module.c_str(), (root / "link.ko").c_str()) == 0 &&
            loadKernelModule(root / "link.ko", "", ::geteuid(), record) == ELOOP && seen.empty(),
        "a symlinked module is not followed");
  check(::mkdir((root / "dir.ko").c_str(), 0755) == 0 &&
            loadKernelModule(root / "dir.ko", "", ::geteuid(), record) == EPERM && seen.empty(),
        "a directory is refused");
  check(loadKernelModule(root / "missing.ko", "", ::geteuid(), record) == ENOENT, "a missing module reports ENOENT");
  const ModuleLoad failing = [](int, const std::string&) { return EEXIST + 1000; };
  check(loadKernelModule(module, "", ::geteuid(), failing) == EEXIST + 1000, "the loader's error is returned");
}

}

int main() {
  panicReboot();
  moduleChecks();
  return tc002d_test::finish("tc002d kernel");
}
