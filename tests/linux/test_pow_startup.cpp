#include <pthread.h>

#include <atomic>
#include <cerrno>
#include <cstdio>
#include <thread>

#include "platform/linux/script/PowScanner.h"

namespace {
std::atomic<bool> failScheduling{false};
std::atomic<unsigned> creations{0};
std::atomic<unsigned> failCreation{0};
}

extern "C" int __real_pthread_setschedparam(pthread_t, int, const sched_param*);
extern "C" int __real_pthread_create(pthread_t*, const pthread_attr_t*, void* (*)(void*), void*);

extern "C" int __wrap_pthread_setschedparam(pthread_t thread, int policy, const sched_param* param) {
  return failScheduling.load() ? EPERM : __real_pthread_setschedparam(thread, policy, param);
}

extern "C" int __wrap_pthread_create(pthread_t* thread, const pthread_attr_t* attr,
                                    void* (*entry)(void*), void* arg) {
  if (++creations == failCreation.load()) return EAGAIN;
  return __real_pthread_create(thread, attr, entry, arg);
}

int main() {
  uint8_t header[80] = {}, target[32] = {};
  awtrix::linux_script::PowScanner scanner;
  int failures = 0;
  auto check = [&](bool ok, const char* message) {
    if (!ok) { ++failures; std::printf("FAIL: %s\n", message); }
  };
  failScheduling = true;
  check(!scanner.start(header, target, 0, 0xffffffffu, 2), "idle scheduling failure refuses the scan");
  awtrix::linux_script::PowEvent event;
  check(!scanner.running() && !scanner.pop(event), "failed scheduling leaves no workers or events");
  failScheduling = false;
  for (unsigned failAt = 1; failAt <= 2 && failAt <= std::thread::hardware_concurrency(); ++failAt) {
    creations = 0;
    failCreation = failAt;
    check(!scanner.start(header, target, 0, 0xffffffffu, 2), "thread creation failure is reported");
    check(!scanner.running() && !scanner.pop(event), "partly started workers are joined on failure");
  }
  failCreation = 0;
  check(scanner.start(header, target, 0, 0xffffffffu, 1), "the scanner recovers after a startup failure");
  scanner.stop();
  check(!scanner.running(), "the recovered scanner stops");
  return failures == 0 ? 0 : 1;
}
