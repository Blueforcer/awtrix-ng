#include "system/Log.h"
#include "system/MonotonicClock.h"
#include "platform/posix/Time.h"
#include "core/api/JsonStream.h"

#include <cstdio>
#include <ctime>
#include <string>
#include <unistd.h>

#include "../support.h"

namespace {
constexpr auto check = awtrix::test::require;
int64_t systemUs() {
  timespec value{};
  check(::clock_gettime(CLOCK_MONOTONIC, &value) == 0, "read system monotonic clock");
  return static_cast<int64_t>(value.tv_sec) * 1000000 + value.tv_nsec / 1000;
}
}

extern "C" time_t __wrap_time(time_t* out) {
  if (out) *out = 0;
  return 0;
}

int main() {
  const int64_t before = systemUs();
  const int64_t application = awtrix::monotonicUs();
  const int64_t fileClock = awtrix::posix::monotonicMs();
  const int64_t after = systemUs();
  check(before <= application && application <= after, "application clock uses the system monotonic epoch");
  check(before / 1000 <= fileClock && fileClock <= after / 1000, "POSIX and application timestamps are comparable");

  FILE* capture = std::tmpfile();
  check(capture != nullptr, "create stderr capture");
  const int saved = ::dup(STDERR_FILENO);
  check(saved >= 0 && ::dup2(::fileno(capture), STDERR_FILENO) >= 0, "capture stderr");
  const int64_t firstSecond = systemUs() / 1000000;
  awtrix::logf("clock before wall time synchronization");
  const int64_t lastSecond = systemUs() / 1000000;
  std::fflush(stderr);
  check(::dup2(saved, STDERR_FILENO) >= 0, "restore stderr");
  ::close(saved);
  std::rewind(capture);
  char line[256]{};
  check(std::fgets(line, sizeof line, capture) != nullptr, "platform output reaches stderr");
  std::fclose(capture);
  unsigned long seconds = 0;
  check(std::sscanf(line, "[%lus]", &seconds) == 1 && seconds >= static_cast<unsigned long>(firstSecond) &&
        seconds <= static_cast<unsigned long>(lastSecond), "early log lines use the shared uptime clock");

  std::string json;
  awtrix::api::JsonStream out([](void* context, const char* text, std::size_t size) {
    static_cast<std::string*>(context)->append(text, size);
  }, &json);
  awtrix::logbuf::streamJsonAfter(0, out);
  check(json.find("clock before wall time synchronization") != std::string::npos,
        "the API ring keeps the emitted log line");
  std::puts("host log: shared clock epoch, uptime stamp, stderr output and API ring passed");
}
