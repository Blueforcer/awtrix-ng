#pragma once

#include <cstdint>
#include <map>
#include <string>
#include <vector>

#include "platform/host/Process.h"
#include "platform/host/Socket.h"
#include "platform/linux/hostd/ClockStore.h"

namespace awtrix::hostd {

// One awtrix-script-runner process per paired clock. A process that ends is started again, after
// one second at first and up to a minute when it keeps failing; a minute of running resets that.
class Children {
 public:
  static constexpr int64_t kFirstBackoffMs = 1000;
  static constexpr int64_t kMaxBackoffMs = 60000;
  static constexpr int64_t kHealthyMs = 60000;

  Children(std::string binary, std::string webui, std::string caFile = {}, int idleSeconds = 0)
      : binary_(std::move(binary)), webui_(std::move(webui)), caFile_(std::move(caFile)), idleSeconds_(idleSeconds) {}
  ~Children() { stopAll(); }
  Children(const Children&) = delete;
  Children& operator=(const Children&) = delete;

  // Starts a process for every clock that has none and stops those of clocks that are gone.
  void sync(const ClockStore& store, int64_t nowMs);
  void awaitAll(const ClockStore& store);
  void restart(const std::string& uid);
  void stop(const std::string& uid);
  void stopAll();
  // Reaps ended processes and starts them again when their backoff ran out.
  void tick(const ClockStore& store, int64_t nowMs);

  bool running(const std::string& uid) const;
  bool waiting(const std::string& uid) const;

 private:
  struct Child {
    os::ChildProcess process;
    int64_t startedMs = 0;
    int64_t restartAtMs = 0;
    int64_t backoffMs = kFirstBackoffMs;
    bool waiting = false;
    os::UniqueSocket door;
  };

  bool launch(const ClockStore& store, const ClockRecord& clock, Child& child, int64_t nowMs);
  static bool heard(const ClockRecord& clock, Child& child);

  std::string binary_;
  std::string webui_;
  std::string caFile_;
  int idleSeconds_ = 0;
  std::map<std::string, Child> children_;
};

}
