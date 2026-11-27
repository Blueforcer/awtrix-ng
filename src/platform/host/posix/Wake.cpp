#include "platform/host/Wake.h"

#include <fcntl.h>
#include <unistd.h>

#include <cerrno>

namespace awtrix::os {
namespace {

bool openPipe(int (&ends)[2]) {
#if defined(__linux__)
  return ::pipe2(ends, O_NONBLOCK | O_CLOEXEC) == 0;
#else
  if (::pipe(ends) != 0) return false;
  for (const int fd : ends) {
    const int flags = ::fcntl(fd, F_GETFL);
    if (flags >= 0 && ::fcntl(fd, F_SETFL, flags | O_NONBLOCK) == 0 && ::fcntl(fd, F_SETFD, FD_CLOEXEC) == 0)
      continue;
    ::close(ends[0]);
    ::close(ends[1]);
    return false;
  }
  return true;
#endif
}

}

Wake::Wake() {
  int ends[2];
  if (!openPipe(ends)) return;
  read_ = ends[0];
  write_ = ends[1];
}

Wake::~Wake() {
  if (read_ >= 0) ::close(read_);
  if (write_ >= 0) ::close(write_);
}

void Wake::signal() {
  if (write_ < 0) return;
  const char one = 1;
  while (::write(write_, &one, 1) < 0 && errno == EINTR) {
  }
}

void Wake::drain() {
  if (read_ < 0) return;
  char bytes[64];
  for (;;) {
    const ssize_t count = ::read(read_, bytes, sizeof bytes);
    if (count > 0 || (count < 0 && errno == EINTR)) continue;
    return;
  }
}

}
