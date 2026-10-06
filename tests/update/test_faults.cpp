// Test-only LD_PRELOAD fixture; never linked into the application or verifier.
#include <cerrno>
#include <cstdlib>
#include <dlfcn.h>
#include <unistd.h>

extern "C" int fsync(int fd) {
  static unsigned long calls = 0;
  const char* selected = std::getenv("AWTRIX_TEST_FAIL_FSYNC_AT");
  if (selected && ++calls == std::strtoul(selected, nullptr, 10)) {
    errno = EIO;
    return -1;
  }
  const auto real = reinterpret_cast<int (*)(int)>(dlsym(RTLD_NEXT, "fsync"));
  if (!real) { errno = EIO; return -1; }
  return real(fd);
}
