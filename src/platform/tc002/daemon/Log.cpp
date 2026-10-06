#include "platform/posix/Files.h"
#include "platform/tc002/daemon/Log.h"

#include <fcntl.h>
#include <poll.h>
#include <sys/stat.h>
#include <unistd.h>

#include <cerrno>
#include <cstdarg>
#include <cstdio>
#include <ctime>

namespace awtrix {
namespace tc002d {
namespace {

struct LogState {
  int fd = -1;
  std::string path;
  std::size_t cap = Log::kDefaultCapBytes;
  bool mirror = true;
  Log::Sink sink;
  bool forwarding = false;
};

LogState& state() {
  static LogState instance;
  return instance;
}

int openFile(const std::string& path, bool truncate) {
  return ::open(path.c_str(), O_WRONLY | O_CREAT | O_APPEND | O_CLOEXEC | O_NOCTTY | (truncate ? O_TRUNC : 0),
                0600);
}

void rotateIfNeeded(std::size_t incoming) {
  auto& s = state();
  if (s.fd < 0) return;
  struct stat info{};
  if (::fstat(s.fd, &info) < 0 || static_cast<std::size_t>(info.st_size) + incoming <= s.cap) return;
  const std::string previous = s.path + ".1";
  ::rename(s.path.c_str(), previous.c_str());
  const int fresh = openFile(s.path, true);
  if (fresh < 0) return;
  ::close(s.fd);
  s.fd = fresh;
}

void emit(const char* component, const char* message, std::size_t length) {
  auto& s = state();
  const int saved = errno;
  timespec now{};
  clock_gettime(CLOCK_MONOTONIC, &now);
  char buffer[Log::kMaxLine + 96];
  int used = std::snprintf(buffer, sizeof buffer, "%lld.%03ld %s: ", static_cast<long long>(now.tv_sec),
                           now.tv_nsec / 1000000, component ? component : "?");
  if (used < 0) used = 0;
  std::size_t at = static_cast<std::size_t>(used) < Log::kMaxLine ? static_cast<std::size_t>(used) : Log::kMaxLine;
  bool truncated = false;
  for (std::size_t i = 0; i < length; ++i) {
    if (at >= Log::kMaxLine) { truncated = true; break; }
    const unsigned char c = static_cast<unsigned char>(message[i]);
    buffer[at++] = (c < 0x20 && c != '\t') || c == 0x7f ? '?' : static_cast<char>(c);
  }
  if (truncated) { buffer[at++] = '.'; buffer[at++] = '.'; buffer[at++] = '.'; }
  buffer[at++] = '\n';
  if (s.fd >= 0) {
    rotateIfNeeded(at);
    posix::writeAll(s.fd, buffer, at);
  }
  if (s.mirror) {
    pollfd item{STDERR_FILENO, POLLOUT, 0};
    if (::poll(&item, 1, 0) == 1 && (item.revents & POLLOUT)) {
      const ssize_t count = ::write(STDERR_FILENO, buffer, at);
      if (count < 0 && (errno == EPIPE || errno == EIO || errno == EBADF)) s.mirror = false;
    }
  }
  if (s.sink && !s.forwarding) {
    s.forwarding = true;
    s.sink(component ? component : "?", std::string_view(message, length));
    s.forwarding = false;
  }
  errno = saved;
}

}

bool Log::open(const std::string& path, std::size_t capBytes) {
  auto& s = state();
  const int fd = openFile(path, false);
  if (fd < 0) return false;
  if (s.fd >= 0) ::close(s.fd);
  s.fd = fd;
  s.path = path;
  s.cap = capBytes < 4096 ? 4096 : capBytes;
  return true;
}

void Log::mirrorToStderr(bool enabled) { state().mirror = enabled; }

void Log::forwardTo(Sink sink) { state().sink = std::move(sink); }

void Log::close() {
  auto& s = state();
  if (s.fd >= 0) ::close(s.fd);
  s.fd = -1;
  s.path.clear();
}

void Log::line(const char* component, const char* format, ...) {
  char message[kMaxLine + 1];
  va_list args;
  va_start(args, format);
  const int length = std::vsnprintf(message, sizeof message, format, args);
  va_end(args);
  if (length < 0) return;
  const std::size_t size = static_cast<std::size_t>(length) < sizeof message ? static_cast<std::size_t>(length)
                                                                              : sizeof message - 1;
  emit(component, message, size);
}

void Log::text(const char* component, std::string_view message) {
  emit(component, message.data(), message.size());
}

}
}
