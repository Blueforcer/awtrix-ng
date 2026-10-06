#pragma once

#include <sys/types.h>

#include <functional>
#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

// The daemon's children, started one way: default signal handling with nothing blocked, exactly
// the descriptors asked for and no other, and stdin, stdout and stderr on /dev/null unless mapped.
namespace awtrix {
namespace tc002d {

struct Descriptor {
  int source;
  int target;
};

enum class ProcessGroup {
  Inherit,
  Own,      // setpgid(0, 0): the daemon signals the group
  Session,  // setsid(): a new session, detached from any terminal
};

struct ProcessSpec {
  std::string path;
  std::vector<std::string> argv;
  std::vector<std::string> environment;
  std::vector<Descriptor> descriptors;
  ProcessGroup group = ProcessGroup::Inherit;
  // Sent to the child when the daemon dies; 0 lets it outlive the daemon.
  int parentDeathSignal = 0;
  // -1 keeps the daemon's.
  int umask = -1;
};

// Returns the pid, or -1 with error set. The child exits 126 when its setup fails, 127 when the
// program cannot be executed.
pid_t spawnProcess(const ProcessSpec& spec, std::string& error);

// Runs task in a forked child with default signals, umask 077, the given parent-death signal and
// no descriptors above 2 but those in keep; the child exits with the low byte of task().
pid_t forkTask(const std::function<int()>& task, int parentDeathSignal, const std::vector<int>& keep = {});

inline constexpr int64_t kReapGraceMs = 1000;
// A child stuck in the kernel never becomes reapable; stop waiting at the monotonic deadline.
void reapUntil(pid_t pid, int64_t deadlineMs);
// Startup/forked child only: preserve stdio and the explicitly listed descriptors.
void closeOthers(const int* keep = nullptr, std::size_t count = 0);

// "exit 3", "signal 9" or "status 0x...".
std::string describeWait(int status);

}
}
