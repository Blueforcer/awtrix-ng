#include "platform/tc002/daemon/Process.h"
#include "platform/posix/Files.h"

#include <fcntl.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <unistd.h>

#include <csignal>
#include <cstdlib>
#include <sstream>

#include "support.h"

using namespace awtrix::tc002d;
using tc002d_test::check;
using tc002d_test::readFile;
using tc002d_test::TempDir;

namespace {

int waitFor(pid_t pid) {
  int status = 0;
  return pid > 0 && ::waitpid(pid, &status, 0) == pid ? status : -1;
}

std::vector<int> descriptors(const std::string& listing) {
  std::vector<int> out;
  std::istringstream entries(listing);
  std::string entry;
  while (entries >> entry) out.push_back(std::atoi(entry.c_str()));
  return out;
}

bool contains(const std::vector<int>& list, int value) {
  for (const int item : list)
    if (item == value) return true;
  return false;
}

void spawnIsolation() {
  TempDir root;
  ::setenv("TC002D_PROCESS_CANARY", "leak", 1);
  const int opened = ::open("/dev/null", O_RDONLY);
  const int stray = ::fcntl(opened, F_DUPFD, 50);
  const int passed = ::fcntl(opened, F_DUPFD, 60);
  ::close(opened);
  ProcessSpec spec;
  spec.path = "/bin/sh";
  spec.argv = {"sh", "-c", "env > \"$0\"; ls /proc/self/fd > \"$1\"; umask >> \"$0\"; exit 7", root / "env.txt",
               root / "fd.txt"};
  spec.environment = {"ONLY=1"};
  spec.descriptors = {{passed, 9}};
  spec.umask = 077;
  std::string error;
  const int status = waitFor(spawnProcess(spec, error));
  check(status >= 0 && WIFEXITED(status) && WEXITSTATUS(status) == 7, "exit status of the program: " + error);
  const std::string env = readFile(root / "env.txt");
  check(env.find("ONLY=1") != std::string::npos && env.find("TC002D_PROCESS_CANARY") == std::string::npos,
        "the environment is exactly what was passed");
  check(env.find("0077") != std::string::npos, "the requested umask");
  const std::vector<int> open = descriptors(readFile(root / "fd.txt"));
  check(contains(open, 9) && !contains(open, stray) && !contains(open, passed) && open.size() <= 5,
        "stdio, the mapped descriptor and ls's own only");
  ::close(stray);
  ::close(passed);
  ::unsetenv("TC002D_PROCESS_CANARY");
}

void spawnFailures() {
  std::string error;
  ProcessSpec empty;
  check(spawnProcess(empty, error) < 0 && !error.empty(), "an empty specification is refused");
  ProcessSpec missing;
  missing.path = "/nonexistent/program";
  missing.argv = {"program"};
  const int status = waitFor(spawnProcess(missing, error));
  check(status >= 0 && WIFEXITED(status) && WEXITSTATUS(status) == 127, "a program that cannot run exits 127");
  ProcessSpec negative;
  negative.path = "/bin/true";
  negative.argv = {"true"};
  negative.descriptors = {{-1, 5}};
  check(spawnProcess(negative, error) < 0, "an invalid mapping is refused");
}

void groups() {
  std::string error;
  ProcessSpec own;
  own.path = "/bin/sh";
  own.argv = {"sh", "-c", "sleep 5"};
  own.group = ProcessGroup::Own;
  const pid_t pid = spawnProcess(own, error);
  check(pid > 0, "own group started: " + error);
  bool grouped = false;
  for (int i = 0; i < 200 && !grouped; ++i) {
    grouped = ::getpgid(pid) == pid;
    if (!grouped) ::usleep(5000);
  }
  check(grouped, "the child leads its own process group");
  ::kill(-pid, SIGKILL);
  waitFor(pid);
  ProcessSpec session = own;
  session.group = ProcessGroup::Session;
  const pid_t leader = spawnProcess(session, error);
  bool separate = false;
  for (int i = 0; i < 200 && !separate; ++i) {
    separate = ::getsid(leader) == leader;
    if (!separate) ::usleep(5000);
  }
  check(separate, "the child leads a new session");
  ::kill(-leader, SIGKILL);
  waitFor(leader);
}

void tasks() {
  int status = waitFor(forkTask([] { return 42; }, SIGKILL));
  check(status >= 0 && WIFEXITED(status) && WEXITSTATUS(status) == 42, "a task's result is its exit code");
  int ends[2];
  check(::pipe(ends) == 0, "pipe");
  const int out = ends[1];
  status = waitFor(forkTask(
      [out] {
        const bool kept = ::fcntl(out, F_GETFD) >= 0;
        const char answer = kept ? 'k' : 'x';
        return ::write(out, &answer, 1) == 1 ? 0 : 1;
      },
      SIGKILL, {out}));
  char answer = 0;
  check(status >= 0 && WIFEXITED(status) && WEXITSTATUS(status) == 0 && ::read(ends[0], &answer, 1) == 1 &&
            answer == 'k',
        "a kept descriptor survives in the task");
  const int reader = ends[0];
  status = waitFor(forkTask([reader] { return ::fcntl(reader, F_GETFD) < 0 ? 0 : 1; }, SIGKILL));
  check(status >= 0 && WIFEXITED(status) && WEXITSTATUS(status) == 0, "other descriptors are closed in the task");
  ::close(ends[0]);
  ::close(ends[1]);
}

void descriptions() {
  check(describeWait(0) == "exit 0", "clean exit");
  check(describeWait(3 << 8) == "exit 3", "exit code");
  check(describeWait(SIGKILL) == "signal 9", "signal");
}

int handlerPipe = -1;

void daemonHandler(int) {
  const char byte = 1;
  (void)!::write(handlerPipe, &byte, 1);
}

// A signal that reaches a child right after fork acts on the child, never through the daemon's
// handler (which, run in the child, would write to the daemon's self-pipe).
void earlySignals() {
  int ends[2];
  if (::pipe2(ends, O_CLOEXEC | O_NONBLOCK) < 0) return check(false, "handler pipe");
  handlerPipe = ends[1];
  struct sigaction action{}, previous{};
  action.sa_handler = daemonHandler;
  sigemptyset(&action.sa_mask);
  ::sigaction(SIGTERM, &action, &previous);
  bool allTerminated = true;
  for (int round = 0; round < 200 && allTerminated; ++round) {
    ProcessSpec spec;
    spec.path = "/bin/sleep";
    spec.argv = {"sleep", "1"};
    std::string error;
    const pid_t pid = spawnProcess(spec, error);
    ::kill(pid, SIGTERM);
    const int status = waitFor(pid);
    allTerminated = allTerminated && status >= 0 && WIFSIGNALED(status) && WTERMSIG(status) == SIGTERM;
  }
  ::sigaction(SIGTERM, &previous, nullptr);
  char byte;
  check(allTerminated, "a signal right after the spawn terminates the child");
  check(::read(ends[0], &byte, 1) < 0, "the daemon's handler never ran in a child");
  ::close(ends[0]);
  ::close(ends[1]);
}

}

void boundedReap() {
  ProcessSpec spec;
  spec.path = "/bin/sleep";
  spec.argv = {"sleep", "10"};
  std::string error;
  const pid_t pid = spawnProcess(spec, error);
  check(pid > 0, "child for bounded reap");
  if (pid <= 0) return;
  reapUntil(pid, awtrix::posix::monotonicMs());
  check(::waitpid(pid, nullptr, WNOHANG) == 0, "expired deadline leaves running child alone");
  ::kill(pid, SIGKILL);
  reapUntil(pid, awtrix::posix::monotonicMs() + kReapGraceMs);
  check(::waitpid(pid, nullptr, WNOHANG) < 0 && errno == ECHILD, "terminated child was reaped");
  reapUntil(pid, awtrix::posix::monotonicMs() + kReapGraceMs); // no longer our child
}

int main() {
  boundedReap();
  spawnIsolation();
  spawnFailures();
  groups();
  tasks();
  descriptions();
  earlySignals();
  return tc002d_test::finish("tc002d process");
}
