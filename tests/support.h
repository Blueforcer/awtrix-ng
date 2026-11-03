#pragma once

#ifdef __cplusplus
#include <sys/wait.h>
#include <unistd.h>

#include <atomic>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <iterator>
#include <string_view>
#include <thread>

namespace awtrix::test {

// The threads of this process, once their number has held for 200 ms: join() returns before the kernel
// takes the thread off the list, and under load that took long enough to fail a test.
inline std::size_t threadCount() {
  const auto listed = [] {
    const std::filesystem::directory_iterator tasks("/proc/self/task");
    return static_cast<std::size_t>(std::distance(begin(tasks), end(tasks)));
  };
  std::size_t count = listed();
  auto since = std::chrono::steady_clock::now();
  while (std::chrono::steady_clock::now() - since < std::chrono::milliseconds(200)) {
    std::this_thread::sleep_for(std::chrono::milliseconds(1));
    const std::size_t now = listed();
    if (now != count) {
      count = now;
      since = std::chrono::steady_clock::now();
    }
  }
  return count;
}

inline int& failures() {
  static int value = 0;
  return value;
}

inline std::atomic<unsigned>& passed() {
  static std::atomic<unsigned> value{0};
  return value;
}

inline void report(std::string_view message) {
  std::fprintf(stderr, "FAIL: %.*s\n", static_cast<int>(message.size()), message.data());
}

// Collect independent failures so a suite can report all affected behavior.
inline void check(bool condition, std::string_view message = "condition failed") {
  if (condition) return;
  report(message);
  ++failures();
}

// Stop when continuing would use a failed setup or an invalid test resource.
inline void require(bool condition, std::string_view message = "condition failed") {
  if (!condition) {
    report(message);
    std::exit(EXIT_FAILURE);
  }
  ++passed();
}

inline pid_t endedProcess() {
  const pid_t pid = ::fork();
  if (pid == 0) ::_exit(0);
  require(pid > 0 && ::waitpid(pid, nullptr, 0) == pid, "the pid of a process that ended");
  return pid;
}

inline int finish(const char* name) {
  if (failures()) {
    std::fprintf(stderr, "%s: %d failure(s)\n", name, failures());
    return EXIT_FAILURE;
  }
  std::printf("%s: ok\n", name);
  return EXIT_SUCCESS;
}

}

#else

#include <stdio.h>
#include <stdlib.h>

static int awtrix_test_failures;
static unsigned awtrix_test_passed;

static inline void awtrix_test_check(int condition, const char* expression,
                                    const char* file, int line, const char* context) {
  if (condition) return;
  fprintf(stderr, "%s: %s:%d: CHECK failed: %s\n", context, file, line, expression);
  ++awtrix_test_failures;
}

static inline void awtrix_test_require(int condition, const char* message) {
  if (!condition) {
    fprintf(stderr, "FAIL: %s\n", message);
    exit(EXIT_FAILURE);
  }
  ++awtrix_test_passed;
}

#define AWTRIX_TEST_CHECK(condition, context) \
  awtrix_test_check(!!(condition), #condition, __FILE__, __LINE__, context)

#endif
