#pragma once

#include <cstdint>
#include <map>
#include <string>
#include <vector>

#include <sys/types.h>

#include "platform/linux/hostd/ClockStore.h"

namespace awtrix::hostd {

// One awtrix-linux --remote process per paired clock. A process that ends is started again, after
// one second at first and up to a minute when it keeps failing; a minute of running resets that.
class Children {
 public:
  static constexpr int64_t kFirstBackoffMs = 1000;
  static constexpr int64_t kMaxBackoffMs = 60000;
  static constexpr int64_t kHealthyMs = 60000;

  Children(std::string binary, std::string webui) : binary_(std::move(binary)), webui_(std::move(webui)) {}
  ~Children() { stopAll(); }
  Children(const Children&) = delete;
  Children& operator=(const Children&) = delete;

  // Starts a process for every clock that has none and stops those of clocks that are gone.
  void sync(const ClockStore& store, int64_t nowMs);
  void restart(const std::string& uid);
  void stop(const std::string& uid);
  void stopAll();
  // Reaps ended processes and starts them again when their backoff ran out.
  void tick(const ClockStore& store, int64_t nowMs);

  bool running(const std::string& uid) const;

 private:
  struct Child {
    pid_t pid = -1;
    int64_t startedMs = 0;
    int64_t restartAtMs = 0;
    int64_t backoffMs = kFirstBackoffMs;
  };

  bool launch(const ClockStore& store, const ClockRecord& clock, Child& child, int64_t nowMs);
  static void terminate(pid_t pid);

  std::string binary_;
  std::string webui_;
  std::map<std::string, Child> children_;
};

}
