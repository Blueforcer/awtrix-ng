#pragma once

#ifdef __cplusplus
#include <atomic>
#include <cstdio>
#include <cstdlib>
#include <string_view>

namespace awtrix::test {

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
