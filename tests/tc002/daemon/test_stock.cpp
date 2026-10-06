// StockWatch against real processes named hciattach and zkgui, with a fake setprop that records
// its calls and, when told to, stops the process the way init would. The watch sees only this
// test's processes: its process table links to them in /proc, so parallel runs stay apart.
#include <fcntl.h>
#include <sys/prctl.h>
#include <sys/stat.h>
#include <sys/wait.h>

#include <csignal>
#include <cstdlib>
#include <memory>
#include <vector>

#include "platform/tc002/daemon/PropertyWorkspace.h"
#include "platform/tc002/daemon/StockApp.h"
#include "support.h"

using namespace awtrix::tc002d;
using tc002d_test::check;
using tc002d_test::exists;
using tc002d_test::makeLink;
using tc002d_test::readFile;
using tc002d_test::runUntil;
using tc002d_test::TempDir;
using tc002d_test::writeFile;

namespace {

constexpr int64_t kIntervalMs = 300;

std::size_t occurrences(const std::string& text, const std::string& needle) {
  std::size_t count = 0;
  for (auto at = text.find(needle); at != std::string::npos; at = text.find(needle, at + 1)) ++count;
  return count;
}

class Workspace {
 public:
  explicit Workspace(const std::string& path) {
    writeFile(path, std::string(4096, '\0'));
    fd_ = ::open(path.c_str(), O_RDONLY);
    variable_ = std::to_string(fd_) + ",4096";
  }
  ~Workspace() {
    withdraw();
    if (fd_ >= 0) ::close(fd_);
  }
  bool offer() {
    ::setenv("ANDROID_PROPERTY_WORKSPACE", variable_.c_str(), 1);
    return PropertyWorkspace::adopt();
  }
  void withdraw() {
    ::unsetenv("ANDROID_PROPERTY_WORKSPACE");
    PropertyWorkspace::adopt();
  }

 private:
  int fd_ = -1;
  std::string variable_;
};

struct Rig {
  TempDir dir;
  std::unique_ptr<StockWatch> watch;
  std::vector<pid_t> children;
  EventLoop loop;

  Rig() {
    const std::string script =
        "#!/bin/sh\n"
        "cd " + dir.path() + " || exit 4\n"
        "fd=${ANDROID_PROPERTY_WORKSPACE%%,*}\n"
        "if [ -z \"$fd\" ] || [ ! -e /proc/$$/fd/$fd ]; then echo \"no fd $*\" >> calls; exit 3; fi\n"
        "echo \"$*\" >> calls\n"
        "if [ -f obey ]; then read pid < pid-$2; kill -9 $pid; fi\n"
        "if [ -f fail ]; then exit 1; fi\n";
    writeFile(dir / "setprop", script);
    ::chmod((dir / "setprop").c_str(), 0755);
    posix::makeDirectories(dir / "proc", 0755);
    Log::open(dir / "daemon.log");
    StockWatchOptions options;
    options.procRoot = dir / "proc";
    options.setprop = dir / "setprop";
    options.intervalMs = kIntervalMs;
    watch = std::make_unique<StockWatch>(options);
    loop.add(*watch, 2000);
    loop.start();
  }
  ~Rig() {
    for (const pid_t pid : children)
      if (::waitpid(pid, nullptr, WNOHANG) == 0) ::kill(pid, SIGKILL);
    loop.requestStop("test end");
    runUntil(loop, [&] { return loop.finished(); }, 3000);
    Log::close();
  }

  pid_t launch(const char* process, const char* service) {
    const pid_t pid = ::fork();
    if (pid == 0) {
      for (int signal : {SIGCHLD, SIGTERM, SIGINT, SIGHUP}) ::signal(signal, SIG_DFL);
      ::prctl(PR_SET_NAME, process, 0, 0, 0);
      for (;;) ::pause();
    }
    writeFile(dir / (std::string("pid-") + service), std::to_string(pid) + "\n");
    makeLink("/proc/" + std::to_string(pid), dir / "proc/" + std::to_string(pid));
    children.push_back(pid);
    return pid;
  }
  // An exited process whose parent never waits for it: it keeps its name in the process table.
  pid_t launchExited(const char* process) {
    int ends[2];
    if (!tc002d_test::makePipe(ends, O_CLOEXEC, "exited process")) return -1;
    const pid_t parent = ::fork();
    if (parent == 0) {
      for (int signal : {SIGCHLD, SIGTERM, SIGINT, SIGHUP}) ::signal(signal, SIG_DFL);
      const pid_t child = ::fork();
      if (child == 0) {
        ::prctl(PR_SET_NAME, process, 0, 0, 0);
        _exit(0);
      }
      (void)!::write(ends[1], &child, sizeof child);
      for (;;) ::pause();
    }
    ::close(ends[1]);
    pid_t child = -1;
    if (parent < 0 || ::read(ends[0], &child, sizeof child) != sizeof child) child = -1;
    ::close(ends[0]);
    if (parent > 0) children.push_back(parent);
    if (child <= 0) return -1;
    const std::string stat = "/proc/" + std::to_string(child) + "/stat";
    const bool exited = runUntil(loop, [&] {
      const std::string text = readFile(stat);
      const auto close = text.rfind(") ");
      return close != std::string::npos && close + 2 < text.size() && text[close + 2] == 'Z';
    }, 2000);
    if (!exited) return -1;
    makeLink("/proc/" + std::to_string(child), dir / "proc/" + std::to_string(child));
    return child;
  }
  bool gone(pid_t pid, int timeoutMs) {
    if (!runUntil(loop, [&] { return !exists("/proc/" + std::to_string(pid)); }, timeoutMs)) return false;
    ::unlink((dir / "proc/" + std::to_string(pid)).c_str());
    return true;
  }
  void idle(int checks) {
    const unsigned until = watch->checks() + static_cast<unsigned>(checks);
    runUntil(loop, [&] { return watch->checks() >= until; }, static_cast<int>(checks * kIntervalMs * 3));
  }
  std::string calls() { return readFile(dir / "calls"); }
  std::string log() { return readFile(dir / "daemon.log"); }
};

void obeyedStop(Workspace& workspace) {
  check(workspace.offer(), "property workspace adopted");
  Rig rig;
  writeFile(rig.dir / "obey", "");
  const pid_t first = rig.launch("hciattach", "hciattach");
  check(rig.gone(first, 3000), "hciattach stopped");
  rig.idle(1);
  check(rig.watch->bluetooth() == StockWatch::Phase::Off, "Bluetooth watch back to off");
  check(rig.calls() == "ctl.stop hciattach\n", "one ctl.stop hciattach with the workspace: " + rig.calls());
  const std::string log = rig.log();
  check(log.find("hciattach running (pid " + std::to_string(first) + "); sent ctl.stop hciattach") !=
            std::string::npos,
        "appearance logged");
  check(log.find("stock: hciattach stopped") != std::string::npos, "stop logged");
  check(log.find("SIGKILL") == std::string::npos, "an obeyed ctl.stop needs no SIGKILL");

  const std::size_t lines = occurrences(rig.log(), "stock:");
  rig.idle(4);
  check(occurrences(rig.log(), "stock:") == lines, "quiet checks log nothing");
  check(rig.calls() == "ctl.stop hciattach\n", "no setprop while nothing runs");

  const pid_t second = rig.launch("hciattach", "hciattach");
  check(rig.gone(second, 3000), "a second hciattach is stopped too");
  check(rig.calls() == "ctl.stop hciattach\nctl.stop hciattach\n", "a new appearance gets its own ctl.stop");
}

void ignoredStop(Workspace& workspace) {
  check(workspace.offer(), "property workspace adopted");
  Rig rig;
  const pid_t pid = rig.launch("hciattach", "hciattach");
  check(rig.gone(pid, 3000), "hciattach that ignores ctl.stop is killed");
  rig.idle(1);
  check(rig.watch->bluetooth() == StockWatch::Phase::Off, "Bluetooth watch back to off after SIGKILL");
  check(rig.calls() == "ctl.stop hciattach\n", "ctl.stop hciattach sent once before SIGKILL: " + rig.calls());
  const std::string log = rig.log();
  check(log.find("hciattach pid " + std::to_string(pid) + " survived ctl.stop hciattach; sending SIGKILL") !=
            std::string::npos,
        "survival logged");
  check(log.find("hciattach gone after SIGKILL") != std::string::npos, "kill logged");
}

void exitedProcessIgnored(Workspace& workspace) {
  check(workspace.offer(), "property workspace adopted");
  Rig rig;
  const pid_t pid = rig.launchExited("hciattach");
  check(pid > 0 && readFile("/proc/" + std::to_string(pid) + "/comm") == "hciattach\n",
        "an exited hciattach keeps its name until its parent waits for it");
  rig.idle(3);
  check(rig.watch->bluetooth() == StockWatch::Phase::Off && rig.calls().empty() &&
            rig.log().find("hciattach running") == std::string::npos,
        "an exited hciattach is not stopped");
}

void failingSetprop(Workspace& workspace) {
  check(workspace.offer(), "property workspace adopted");
  Rig rig;
  writeFile(rig.dir / "fail", "");
  const pid_t pid = rig.launch("hciattach", "hciattach");
  check(rig.gone(pid, 3000), "hciattach killed after setprop failed");
  check(rig.log().find("setprop ctl.stop hciattach failed (exit 1)") != std::string::npos, "setprop failure logged");
}

void manufacturerLeftAlone(Workspace& workspace) {
  check(workspace.offer(), "property workspace adopted");
  Rig rig;
  writeFile(rig.dir / "obey", "");
  const pid_t app = rig.launch("zkgui", "zkswe");
  const pid_t bluetooth = rig.launch("hciattach", "hciattach");
  check(rig.gone(bluetooth, 3000), "hciattach is stopped");
  rig.idle(3);
  check(exists("/proc/" + std::to_string(app)), "zkgui is left alone");
  check(rig.calls() == "ctl.stop hciattach\n", "no ctl.stop zkswe: " + rig.calls());
  check(rig.log().find("keeping hciattach off") != std::string::npos, "scope logged at start");
}

void withoutWorkspace(Workspace& workspace) {
  workspace.withdraw();
  Rig rig;
  const pid_t pid = rig.launch("hciattach", "hciattach");
  check(rig.gone(pid, 3000), "hciattach killed without a property workspace");
  check(rig.calls().empty(), "setprop is not run without a workspace");
  const std::string log = rig.log();
  check(log.find("no property workspace") != std::string::npos, "missing workspace logged at start");
  check(log.find("ctl.stop hciattach unavailable, sending SIGKILL") != std::string::npos, "direct kill logged");
}

void refusedWorkspaces() {
  for (const char* bad : {"", "8", "2,32768", "8,0", "8,32768x", "x,1", "99999999999,1"}) {
    ::setenv("ANDROID_PROPERTY_WORKSPACE", bad, 1);
    check(!PropertyWorkspace::adopt() && !PropertyWorkspace::available(), std::string("workspace refused: ") + bad);
  }
  ::unsetenv("ANDROID_PROPERTY_WORKSPACE");
  awtrix::tc002d::ProcessSpec spec;
  check(!PropertyWorkspace::adopt() && !PropertyWorkspace::setprop("/bin/setprop", "a", "b", spec),
        "no setprop without a workspace");
}

}

int main() {
  tc002d_test::quietLogs();
  TempDir home;
  Workspace workspace(home / "properties");
  obeyedStop(workspace);
  ignoredStop(workspace);
  exitedProcessIgnored(workspace);
  failingSetprop(workspace);
  manufacturerLeftAlone(workspace);
  withoutWorkspace(workspace);
  refusedWorkspaces();
  return tc002d_test::finish("tc002d stock");
}
