#include "platform/tc002/daemon/Process.h"

#include <dirent.h>
#include <fcntl.h>
#include <sys/prctl.h>
#include <sys/resource.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <unistd.h>

#include <algorithm>
#include <cerrno>
#include <csignal>
#include <cstdio>
#include <cstdlib>
#include <cstring>

#include "platform/posix/Files.h"

namespace awtrix {
namespace tc002d {
namespace {

constexpr std::size_t kMaxDescriptors = 16;

// Forks with every signal blocked, so the child never runs one of the daemon's handlers; a signal
// sent to it before resetSignals() stays pending and then acts on the child as it should.
pid_t forkBlocked() {
  sigset_t all, previous;
  sigfillset(&all);
  sigprocmask(SIG_SETMASK, &all, &previous);
  const pid_t pid = fork();
  if (pid != 0) {
    const int saved = errno;
    sigprocmask(SIG_SETMASK, &previous, nullptr);
    errno = saved;
  }
  return pid;
}

// Forked child only: the handlers go back to default before the mask opens.
void resetSignals() {
  struct sigaction standard{};
  standard.sa_handler = SIG_DFL;
  sigemptyset(&standard.sa_mask);
  for (int number = 1; number < NSIG; ++number)
    if (number != SIGKILL && number != SIGSTOP) sigaction(number, &standard, nullptr);
  sigset_t none;
  sigemptyset(&none);
  sigprocmask(SIG_SETMASK, &none, nullptr);
}

// Forked child only: false when the daemon is gone before the death signal was armed.
bool watchParent(int deathSignal, pid_t parent) {
  if (!deathSignal) return true;
  prctl(PR_SET_PDEATHSIG, deathSignal);
  return getppid() == parent;
}

[[noreturn]] void runChild(const ProcessSpec& spec, pid_t parent, char* const* argv, char* const* envp) {
  resetSignals();
  if (spec.group == ProcessGroup::Own) setpgid(0, 0);
  else if (spec.group == ProcessGroup::Session) setsid();
  if (!watchParent(spec.parentDeathSignal, parent)) _exit(126);
  if (spec.umask >= 0) umask(static_cast<mode_t>(spec.umask));
  // Copies above every target first, so no mapping overwrites the source of another.
  const std::size_t count = spec.descriptors.size();
  int floor = STDERR_FILENO + 1;
  for (const Descriptor& descriptor : spec.descriptors) floor = std::max(floor, descriptor.target + 1);
  int staged[kMaxDescriptors];
  for (std::size_t i = 0; i < count; ++i) {
    staged[i] = fcntl(spec.descriptors[i].source, F_DUPFD, floor);
    if (staged[i] < 0) _exit(126);
  }
  const auto mapped = [&](int fd) {
    for (const Descriptor& descriptor : spec.descriptors)
      if (descriptor.target == fd) return true;
    return false;
  };
  const int null = open("/dev/null", O_RDWR | O_CLOEXEC);
  if (null < 0) _exit(126);
  for (int fd = STDIN_FILENO; fd <= STDERR_FILENO; ++fd)
    if (!mapped(fd) && dup2(null, fd) != fd) _exit(126);
  for (std::size_t i = 0; i < count; ++i) {
    if (dup2(staged[i], spec.descriptors[i].target) != spec.descriptors[i].target) _exit(126);
    staged[i] = spec.descriptors[i].target;
  }
  closeOthers(staged, count);
  execve(spec.path.c_str(), argv, envp);
  dprintf(STDERR_FILENO, "exec %s failed: %s\n", spec.path.c_str(), std::strerror(errno));
  _exit(127);
}

std::vector<char*> pointers(const std::vector<std::string>& values) {
  std::vector<char*> out;
  for (const std::string& value : values) out.push_back(const_cast<char*>(value.c_str()));
  out.push_back(nullptr);
  return out;
}

}

// A child stuck in the kernel never becomes reapable; after the deadline it is left to init.
void reapUntil(pid_t pid, int64_t deadlineMs) {
  int status = 0;
  for (;;) {
    const pid_t reaped = ::waitpid(pid, &status, WNOHANG);
    if (reaped == pid || (reaped < 0 && errno != EINTR)) return;
    if (posix::monotonicMs() >= deadlineMs) return;
    ::usleep(10000);
  }
}


void closeOthers(const int* preserved, std::size_t count) {
  const auto keep = [&](int fd) {
    for (std::size_t i = 0; i < count; ++i) if (preserved[i] == fd) return true;
    return false;
  };
  for (int pass = 0; pass < 16; ++pass) {
    DIR* directory = opendir("/proc/self/fd");
    if (!directory) break;
    int found[256];
    std::size_t closing = 0;
    const int own = dirfd(directory);
    while (const dirent* entry = readdir(directory)) {
      if (!posix::numeric(entry->d_name)) continue;
      const int fd = std::atoi(entry->d_name);
      if (fd > STDERR_FILENO && fd != own && !keep(fd) && closing < sizeof found / sizeof found[0])
        found[closing++] = fd;
    }
    closedir(directory);
    for (std::size_t i = 0; i < closing; ++i) close(found[i]);
    if (closing < sizeof found / sizeof found[0]) return;
  }
  rlimit limit{};
  long highest = 1024;
  if (getrlimit(RLIMIT_NOFILE, &limit) == 0 && limit.rlim_cur != RLIM_INFINITY)
    highest = static_cast<long>(limit.rlim_cur);
  if (highest > 65536) highest = 65536;
  for (long fd = STDERR_FILENO + 1; fd < highest; ++fd)
    if (!keep(static_cast<int>(fd))) close(static_cast<int>(fd));
}

pid_t spawnProcess(const ProcessSpec& spec, std::string& error) {
  if (spec.path.empty() || spec.argv.empty() || spec.descriptors.size() > kMaxDescriptors) {
    error = "invalid process specification";
    return -1;
  }
  for (const Descriptor& descriptor : spec.descriptors) {
    if (descriptor.source < 0 || descriptor.target < 0) {
      error = "invalid descriptor mapping";
      return -1;
    }
  }
  const std::vector<char*> argv = pointers(spec.argv);
  const std::vector<char*> envp = pointers(spec.environment);
  const pid_t parent = getpid();
  const pid_t pid = forkBlocked();
  if (pid == 0) runChild(spec, parent, argv.data(), envp.data());
  if (pid < 0) {
    error = std::string("fork: ") + std::strerror(errno);
    return -1;
  }
  if (spec.group == ProcessGroup::Own) setpgid(pid, pid);
  return pid;
}

pid_t forkTask(const std::function<int()>& task, int parentDeathSignal, const std::vector<int>& keep) {
  const pid_t parent = getpid();
  const pid_t pid = forkBlocked();
  if (pid != 0) return pid;
  resetSignals();
  if (!watchParent(parentDeathSignal, parent)) _exit(126);
  umask(077);
  closeOthers(keep.data(), keep.size());
  _exit(task() & 0xff);
}

std::string describeWait(int status) {
  char text[32];
  if (WIFEXITED(status)) std::snprintf(text, sizeof text, "exit %d", WEXITSTATUS(status));
  else if (WIFSIGNALED(status)) std::snprintf(text, sizeof text, "signal %d", WTERMSIG(status));
  else std::snprintf(text, sizeof text, "status 0x%x", static_cast<unsigned>(status));
  return text;
}

}
}
