#pragma once

#include <sys/types.h>

#include <cstdint>
#include <functional>
#include <string>

#include "platform/tc002/daemon/Options.h"

// Decides once per daemon start whether the speaker runs: awtrix_pcm is loaded by a short-lived
// child unless the kernel already lists it, and its helper becomes the speaker. Any failure of
// the helper, the module or its load turns the speaker off until the next boot, with the reason
// in note(). An empty pcm.helper means no speaker is configured.
namespace awtrix {
namespace tc002d {

struct SpeakerOptions {
  PcmBackendPaths pcm;
  uid_t owner = 0;
  int64_t loadTimeoutMs = 10000;
  // Runs in the forked loader on the opened module; returns 0 or an errno value. Empty: finit_module.
  std::function<int(int fd)> loadModule;
};

class SpeakerBackend {
 public:
  enum class Kind { None, Loading, Pcm, Off };

  explicit SpeakerBackend(SpeakerOptions options);
  ~SpeakerBackend();
  SpeakerBackend(const SpeakerBackend&) = delete;
  SpeakerBackend& operator=(const SpeakerBackend&) = delete;

  void begin(int64_t nowMs);
  bool pending() const { return kind_ == Kind::Loading; }
  int64_t nextDeadlineMs() const;
  void onTime(int64_t nowMs);
  bool onChildExit(pid_t pid, int status, int64_t nowMs);
  void helperEnded(int status);

  Kind kind() const { return kind_; }
  const char* kindName() const;
  // The helper to start; empty while the module loads and while the speaker is off.
  const std::string& helper() const;
  const std::string& note() const { return note_; }
  pid_t loaderPid() const { return loader_; }

 private:
  void choose(Kind kind, const std::string& note);
  void startLoader(int64_t nowMs);
  int moduleListed() const;

  SpeakerOptions options_;
  Kind kind_ = Kind::None;
  std::string note_;
  pid_t loader_ = -1;
  int64_t loadStartedAt_ = 0;
  int64_t loadDeadline_ = -1;
};

}
}
