// AutostartService against real shell scripts: which files it refuses, what the program gets, the
// restart policy, the log budget and that nothing it started outlives a stop.
#include <signal.h>
#include <sys/stat.h>
#include <unistd.h>

#include <memory>

#include "platform/tc002/daemon/Autostart.h"
#include "support.h"

using namespace awtrix::tc002d;
using tc002d_test::check;
using tc002d_test::makeLink;
using tc002d_test::readFile;
using tc002d_test::runUntil;
using tc002d_test::TempDir;
using tc002d_test::writeFile;
using State = AutostartService::State;

namespace {

struct Rig {
  TempDir dir;
  AutostartOptions options;
  std::unique_ptr<AutostartService> service;
  EventLoop loop;

  Rig() {
    Log::open(dir / "daemon.log");
    options.path = dir / "autostart";
    options.owner = ::getuid();
    options.environment = {"PATH=/bin:/usr/bin", "HOME=" + dir.path()};
    options.backoffMs = {50};
    options.stopGraceMs = 300;
  }
  ~Rig() {
    if (!loop.finished()) {
      loop.requestStop("test over");
      runUntil(loop, [&] { return loop.finished(); }, 3000);
    }
    Log::close();
  }
  void script(const std::string& body, mode_t mode = 0700) {
    writeFile(options.path, "#!/bin/sh\n" + body);
    ::chmod(options.path.c_str(), mode);
  }
  AutostartService& begin() {
    service = std::make_unique<AutostartService>(options);
    loop.add(*service, 2000);
    loop.start();
    return *service;
  }
  std::string log() const { return readFile(dir / "daemon.log"); }
};

bool alive(pid_t pid) { return pid > 0 && ::kill(pid, 0) == 0; }

void absentFile() {
  Rig rig;
  AutostartService& service = rig.begin();
  rig.loop.runOnce(20);
  check(service.state() == State::None && service.starts() == 0, "no file: nothing starts");
}

void refusals() {
  const struct {
    const char* what;
    std::function<void(Rig&)> make;
  } cases[] = {
      {"not executable", [](Rig& rig) { rig.script("exit 0\n", 0600); }},
      {"writable by group or others", [](Rig& rig) { rig.script("exit 0\n", 0770); }},
      {"not a regular file", [](Rig& rig) {
         writeFile(rig.dir / "real", "#!/bin/sh\nexit 0\n");
         ::chmod((rig.dir / "real").c_str(), 0700);
         makeLink(rig.dir / "real", rig.options.path);
       }},
      {"not root", [](Rig& rig) {
         rig.script("exit 0\n");
         rig.options.owner = ::getuid() + 1;
       }},
  };
  for (const auto& entry : cases) {
    Rig rig;
    entry.make(rig);
    AutostartService& service = rig.begin();
    rig.loop.runOnce(20);
    check(service.state() == State::Refused && service.starts() == 0,
          std::string("refused: ") + entry.what + " (" + service.refusal() + ")");
    check(rig.log().find("not starting") != std::string::npos, std::string("refusal logged: ") + entry.what);
  }
}

void cleanExit() {
  Rig rig;
  rig.script("echo \"out $PATH $HOME\"\necho err >&2\nread line && echo \"stdin $line\"\nexit 0\n");
  AutostartService& service = rig.begin();
  check(runUntil(rig.loop, [&] { return service.state() == State::Finished; }, 3000), "exit 0 finishes it");
  rig.loop.runOnce(200);
  const std::string log = rig.log();
  check(log.find("autostart: out /bin:/usr/bin " + rig.dir.path()) != std::string::npos, "stdout logged, environment passed");
  check(log.find("autostart: err") != std::string::npos, "stderr logged");
  check(log.find("stdin") == std::string::npos, "stdin is empty");
  check(service.starts() == 1, "exit 0 is not restarted");
}

void backgroundOutputSurvivesTheExit() {
  Rig rig;
  rig.script("(sleep 1; echo late; echo done > \"$HOME/late\") &\nexit 0\n");
  AutostartService& service = rig.begin();
  check(runUntil(rig.loop, [&] { return tc002d_test::exists(rig.dir / "late"); }, 5000),
        "a background process of the program writes after the program ended");
  rig.loop.runOnce(100);
  check(service.state() == State::Finished && rig.log().find("autostart: late") != std::string::npos,
        "its output is still logged");
}

void givesUp() {
  Rig rig;
  rig.options.maxFailures = 3;
  rig.script("exit 3\n");
  AutostartService& service = rig.begin();
  check(runUntil(rig.loop, [&] { return service.state() == State::GaveUp; }, 3000), "gives up after repeated failures");
  rig.loop.runOnce(200);
  check(service.starts() == 3, "three starts before giving up, got " + std::to_string(service.starts()));
}

void stableRunsKeepRestarting() {
  Rig rig;
  rig.options.maxFailures = 2;
  rig.options.stableMs = 0;
  rig.script("exit 1\n");
  AutostartService& service = rig.begin();
  check(runUntil(rig.loop, [&] { return service.starts() >= 4; }, 3000) && service.state() != State::GaveUp,
        "runs that count as stable never exhaust the failure budget");
}

void stopEndsTheGroup() {
  Rig rig;
  rig.script("sleep 100 &\necho $! > \"$HOME/child\"\nexec sleep 100\n");
  AutostartService& service = rig.begin();
  check(runUntil(rig.loop, [&] { return !readFile(rig.dir / "child").empty(); }, 3000), "program running");
  const pid_t leader = service.pid();
  const pid_t child = std::atoi(readFile(rig.dir / "child").c_str());
  rig.loop.requestStop("test");
  check(runUntil(rig.loop, [&] { return rig.loop.finished(); }, 3000), "stop completes");
  check(runUntil(rig.loop, [&] { return !alive(child); }, 1000), "background child of the program is gone");
  check(!alive(leader), "program is gone");
}

void stubbornProgramIsKilled() {
  Rig rig;
  rig.script("trap '' TERM\necho up\nwhile :; do sleep 1; done\n");
  AutostartService& service = rig.begin();
  check(runUntil(rig.loop, [&] { return rig.log().find("autostart: up") != std::string::npos; }, 3000), "program up");
  rig.loop.requestStop("test");
  check(runUntil(rig.loop, [&] { return rig.loop.finished(); }, 3000), "stop completes after the grace time");
  check(service.pid() <= 0 && rig.log().find("ignored SIGTERM") != std::string::npos, "killed after ignoring SIGTERM");
}

void restartCommand() {
  Rig rig;
  rig.options.maxFailures = 1;
  rig.script("exit 5\n");
  AutostartService& service = rig.begin();
  check(runUntil(rig.loop, [&] { return service.state() == State::GaveUp; }, 3000), "gave up");
  rig.script("exec sleep 100\n");
  service.restart(posix::monotonicMs());
  check(service.state() == State::Running && service.starts() == 2, "restart starts it again after giving up");
  const pid_t first = service.pid();
  service.restart(posix::monotonicMs());
  check(runUntil(rig.loop, [&] { return service.pid() > 0 && service.pid() != first; }, 3000),
        "restart replaces a running program");
  check(service.starts() == 3, "one more start");
}

void logBudget() {
  Rig rig;
  rig.options.logBurst = 5;
  rig.script("i=0\nwhile [ $i -lt 20 ]; do echo line$i; i=$((i+1)); done\n");
  AutostartService& service = rig.begin();
  check(runUntil(rig.loop, [&] { return service.state() == State::Finished; }, 3000), "chatty program finished");
  const std::string log = rig.log();
  check(log.find("autostart: line4") != std::string::npos && log.find("autostart: line5") == std::string::npos,
        "only the burst is logged");
  check(log.find("15 output lines not logged") != std::string::npos, "the rest is counted");
}

}

int main() {
  ::signal(SIGPIPE, SIG_IGN);
  tc002d_test::quietLogs();
  absentFile();
  refusals();
  cleanExit();
  backgroundOutputSurvivesTheExit();
  givesUp();
  stableRunsKeepRestarting();
  stopEndsTheGroup();
  stubbornProgramIsKilled();
  restartCommand();
  logBudget();
  return tc002d_test::finish("tc002d-autostart");
}
