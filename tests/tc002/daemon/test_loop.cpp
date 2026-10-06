// EventLoop ordering, signals and child reaping; bounded log; CLI parsing; status document.
#include <sys/prctl.h>
#include <sys/stat.h>
#include <sys/wait.h>

#include <csignal>
#include <vector>

#include "core/api/JsonReader.h"
#include "platform/tc002/daemon/Options.h"
#include "platform/tc002/daemon/Status.h"
#include "support.h"

using namespace awtrix::tc002d;
using tc002d_test::check;
using tc002d_test::readFile;
using tc002d_test::runUntil;
using tc002d_test::TempDir;
using tc002d_test::writeFile;

namespace {

std::size_t occurrencesOf(const std::string& text, const std::string& needle) {
  std::size_t count = 0;
  for (auto at = text.find(needle); at != std::string::npos; at = text.find(needle, at + 1)) ++count;
  return count;
}

class Recorder : public Service {
 public:
  Recorder(const char* name, std::vector<std::string>& events) : name_(name), events_(events) {}
  const char* name() const override { return name_; }
  bool start(int64_t) override {
    events_.push_back(std::string("start ") + name_);
    return startResult;
  }
  int64_t nextDeadlineMs() const override { return deadline; }
  void onTime(int64_t nowMs) override {
    deadline = -1;
    firedAt = nowMs;
  }
  bool onChildExit(pid_t pid, int status, int64_t) override {
    if (pid != claim) return false;
    claimedStatus = status;
    claim = -1;
    return true;
  }
  void requestStop(int64_t) override {
    events_.push_back(std::string("stop ") + name_);
    stopRequested = true;
  }
  bool stopped() const override { return stopRequested && !neverStops; }

  bool startResult = true;
  bool neverStops = false;
  bool stopRequested = false;
  int64_t deadline = -1;
  int64_t firedAt = -1;
  pid_t claim = -1;
  int claimedStatus = -1;

 private:
  const char* name_;
  std::vector<std::string>& events_;
};

void ordering() {
  std::vector<std::string> events;
  Recorder a("a", events), b("b", events), c("c", events);
  c.neverStops = true;
  EventLoop loop;
  loop.add(a);
  loop.add(b);
  loop.add(c, 200);
  check(loop.start(), "all services start");
  const int64_t begin = posix::monotonicMs();
  b.deadline = begin + 50;
  runUntil(loop, [&] { return b.firedAt >= 0; }, 1000);
  check(b.firedAt >= begin + 50 && b.firedAt < begin + 500, "deadline fires on time");

  ::kill(::getpid(), SIGHUP);
  runUntil(loop, [] { return false; }, 50);
  check(!loop.stopping(), "SIGHUP is ignored");

  const pid_t child = ::fork();
  if (child == 0) _exit(7);
  a.claim = child;
  runUntil(loop, [&] { return a.claim < 0; }, 2000);
  check(WIFEXITED(a.claimedStatus) && WEXITSTATUS(a.claimedStatus) == 7, "SIGCHLD offers the exit to services");

  ::kill(::getpid(), SIGTERM);
  runUntil(loop, [&] { return loop.finished(); }, 3000);
  check(loop.stopping() && loop.stopReason().find("signal") == 0, "SIGTERM requests an orderly stop");
  check(events == std::vector<std::string>({"start a", "start b", "start c", "stop c", "stop b", "stop a"}),
        "reverse stop order");
  check(loop.finished() && !loop.runOnce(0), "loop finished after a stuck service timed out");
}

void stopWaits() {
  std::vector<std::string> events;
  Recorder slow("slow", events), quick("quick", events);
  slow.neverStops = true;
  EventLoop loop;
  loop.add(slow, 400);
  loop.add(quick, 50);
  check(loop.start(), "services start");
  loop.requestStop("test");
  const int64_t begin = posix::monotonicMs();
  unsigned iterations = 0;
  while (loop.runOnce(1000)) ++iterations;
  const int64_t took = posix::monotonicMs() - begin;
  check(took >= 400 && took < 900, "stop waited for the slow service's timeout: " + std::to_string(took) + " ms");
  check(iterations < 20, "an already stopped service's deadline does not wake the loop: " +
                             std::to_string(iterations) + " iterations");
}

void startFailure() {
  std::vector<std::string> events;
  Recorder a("a", events), b("b", events), c("c", events);
  b.startResult = false;
  EventLoop loop;
  loop.add(a);
  loop.add(b);
  loop.add(c);
  check(!loop.start(), "start failure reported");
  loop.run();
  check(events == std::vector<std::string>({"start a", "start b", "stop b", "stop a"}),
        "services after the failed one never start; started ones stop in reverse");
}

void orphans() {
  ::prctl(PR_SET_CHILD_SUBREAPER, 1, 0, 0, 0);
  std::vector<std::string> events;
  Recorder a("a", events);
  EventLoop loop;
  loop.add(a);
  loop.start();
  const pid_t middle = ::fork();
  if (middle == 0) {
    if (::fork() == 0) {
      ::usleep(50000);
      _exit(0);
    }
    _exit(0);
  }
  runUntil(loop, [&] { return loop.unclaimedChildren() >= 2; }, 2000);
  check(loop.unclaimedChildren() == 2, "orphaned grandchildren are reaped through the subreaper");
  ::prctl(PR_SET_CHILD_SUBREAPER, 0, 0, 0, 0);
}

void boundedLog() {
  TempDir dir;
  const std::string path = dir / "daemon.log";
  check(Log::open(path, 4096), "log opens");
  for (int i = 0; i < 200; ++i) Log::line("test", "line %03d %s", i, "0123456789012345678901234567890123456789");
  Log::text("test", std::string("bell\x07 and\nnewline"));
  Log::close();
  struct stat current{}, rotated{};
  check(::stat(path.c_str(), &current) == 0 && current.st_size <= 4096, "log stays under its cap");
  check(::stat((path + ".1").c_str(), &rotated) == 0 && rotated.st_size <= 4096, "one rotation kept");
  const std::string text = readFile(path);
  check(text.find("test: line 199 ") != std::string::npos, "latest line present");
  check(text.find("bell? and?newline\n") != std::string::npos, "control characters replaced");
  const auto firstSpace = text.find(' ');
  check(firstSpace != std::string::npos && text.find('.') < firstSpace, "lines start with monotonic seconds");
}

void options() {
  DaemonOptions options;
  std::string error;
  const char* good[] = {"awtrix-tc002d", "--root", "/tmp/awtrix-release/", "--data", "/data/awtrix-ng", "--boot",
                        "--uart", "/dev/ttyS2", "--http-port", "8080"};
  check(parseDaemonOptions(10, good, options, error), "full option set parses");
  check(options.root == "/tmp/awtrix-release" && options.uart == "/dev/ttyS2" && options.httpPort == 8080,
        "options applied");
  check(options.controlPath() == "/tmp/awtrix-tc002d/control.sock" && options.logPath() == "/tmp/awtrix-tc002d/daemon.log",
        "default run directory paths");
  const char* plain[] = {"x", "--root", "/r", "--data", "/d"};
  check(!parseDaemonOptions(5, plain, options, error) && error.find("--boot is required") == 0,
        "the daemon runs only as the boot service the loader starts");
  const char* factory[] = {"x", "--root", "/r", "--data", "/d", "--boot", "--factory", "no-install"};
  check(!parseDaemonOptions(8, factory, options, error) && error.find("--factory") != std::string::npos,
        "there is no factory release to name");
  const char* relative[] = {"x", "--root", "r", "--data", "/d"};
  check(!parseDaemonOptions(5, relative, options, error), "relative root refused");
  const char* port[] = {"x", "--root", "/r", "--data", "/d", "--http-port", "70000"};
  check(!parseDaemonOptions(7, port, options, error), "port range");
  const char* unknown[] = {"x", "--root", "/r", "--data", "/d", "--frobnicate"};
  check(!parseDaemonOptions(6, unknown, options, error) && error.find("frobnicate") != std::string::npos, "unknown option");
  const char* boot[] = {"x", "--root", "/r", "--data", "/d", "--boot"};
  check(parseDaemonOptions(6, boot, options, error) && options.httpPort == 80 && options.uart == "/dev/ttyS1",
        "boot defaults");
  check(options.bootAttemptsPaths() == std::vector<std::string>{"/d/state/boot-attempts", "/tmp/awtrix-loader.attempts"},
        "the start counters of the release");
  check(options.bootIntro == BootIntro::First && options.introStarts() == IntroStarts::First &&
            options.bootSoundPath() == "/r/share/boot.mp3",
        "a boot shows the intro with the release's sound on the first runtime start");
  check(options.speechVoicePath() == "/r/share/speech/voice.atts", "the release's voice");
  const char* never[] = {"x", "--root", "/r", "--data", "/d", "--boot", "--boot-intro", "never"};
  check(parseDaemonOptions(8, never, options, error) && options.introStarts() == IntroStarts::None &&
            std::string(bootIntroName(options.bootIntro)) == "never",
        "--boot-intro never switches it off");
  const char* always[] = {"x", "--root", "/r", "--data", "/d", "--boot", "--boot-intro", "always"};
  check(parseDaemonOptions(8, always, options, error) && options.introStarts() == IntroStarts::Every,
        "--boot-intro always shows it on every start");
  {
    char dir[] = "/tmp/tc002d-intro-XXXXXX";
    check(::mkdtemp(dir) != nullptr, "intro marker directory");
    const std::string marker = std::string(dir) + "/shown";
    check(claimPowerOnIntro(IntroStarts::First, marker) == IntroStarts::First,
          "the first daemon after power-on claims the intro");
    check(claimPowerOnIntro(IntroStarts::First, marker) == IntroStarts::None,
          "a daemon restarted in boot mode (deploy, zkswe restart) shows no second intro");
    check(claimPowerOnIntro(IntroStarts::Every, marker) == IntroStarts::Every &&
              claimPowerOnIntro(IntroStarts::None, marker) == IntroStarts::None,
          "always and never ignore the power-on marker");
    ::unlink(marker.c_str());
    ::rmdir(dir);
  }
  const char* badIntro[] = {"x", "--root", "/r", "--data", "/d", "--boot-intro", "sometimes"};
  check(!parseDaemonOptions(7, badIntro, options, error) && error == "--boot-intro must be first, always or never",
        "unknown --boot-intro value");
  const char* noIntro[] = {"x", "--root", "/r", "--data", "/d", "--boot-intro"};
  check(!parseDaemonOptions(6, noIntro, options, error), "--boot-intro needs a value");

  CtlOptions ctl;
  const char* status[] = {"ctl", "status"};
  check(parseCtlOptions(2, status, ctl, error) && ctl.command == "status" && ctl.payload.empty(), "ctl status");
  const char* payload[] = {"ctl", "--run-dir", "/tmp/x", "--timeout", "500", "wifi-set", "-"};
  check(parseCtlOptions(7, payload, ctl, error) && ctl.payloadFromStdin && ctl.timeoutMs == 500 &&
            ctl.controlPath() == "/tmp/x/control.sock",
        "ctl options");
  const char* extra[] = {"ctl", "a", "b", "c"};
  check(!parseCtlOptions(4, extra, ctl, error), "ctl refuses extra arguments");
}

void developerFlag() {
  TempDir dir;
  const char* args[] = {"x", "--root", "/r", "--data", "/data/awtrix-ng", "--boot"};
  DaemonOptions options;
  std::string error;
  check(parseDaemonOptions(6, args, options, error) &&
            options.keepAdbTcpFlagPath() == "/data/awtrix-ng/state/keep-adb-tcp",
        "developer flag lives in the state directory");
  const std::string flag = dir / "state/keep-adb-tcp";
  check(!developerFlagPresent(flag, ::geteuid()), "missing flag is off");
  writeFile(flag, "");
  check(developerFlagPresent(flag, ::geteuid()), "regular file owned by the expected user is on");
  check(::geteuid() == 0 || !developerFlagPresent(flag), "a file root does not own is off");
  writeFile(dir / "elsewhere", "");
  ::unlink(flag.c_str());
  check(::symlink((dir / "elsewhere").c_str(), flag.c_str()) == 0 && !developerFlagPresent(flag, ::geteuid()),
        "a symlink is off");
  ::unlink(flag.c_str());
  check(::mkdir(flag.c_str(), 0700) == 0 && !developerFlagPresent(flag, ::geteuid()), "a directory is off");
}

void releaseComponents() {
  TempDir dir;
  DaemonOptions options;
  options.root = dir.path();
  const PcmBackendPaths pcm = options.pcmBackend();
  check(options.runtimePath() == dir / "bin/awtrix-linux" && options.supplicantPath() == dir / "bin/wpa_supplicant" &&
            options.udhcpcPath() == dir / "bin/udhcpc" && options.dhcpCallbackPath() == dir / "bin/dhcp-callback" &&
            pcm.helper == dir / "bin/awtrix-tc002-audio-pcm" && options.moduleDirectory() == dir / "lib/modules" &&
            pcm.module == dir / "lib/modules/awtrix_pcm.ko",
        "every program and module comes from the release");
  const std::vector<std::string> programs = {"bin/awtrix-linux", "bin/wpa_supplicant", "bin/udhcpc",
                                             "bin/dhcp-callback", "bin/awtrix-tc002-audio-pcm"};
  const std::vector<std::string> modules = {"lib/modules/aic8800_bsp.ko", "lib/modules/aic8800_fdrv.ko",
                                            "lib/modules/awtrix_pcm.ko"};
  check(options.missingComponents().size() == programs.size() + modules.size(), "an empty release lacks everything");
  for (const std::string& program : programs) {
    writeFile(dir / program, "#!/bin/sh\n");
    ::chmod((dir / program).c_str(), 0755);
  }
  for (const std::string& module : modules) writeFile(dir / module, "module");
  check(options.missingComponents().empty(), "a complete release");
  ::chmod((dir / "bin/wpa_supplicant").c_str(), 0644);
  check(options.missingComponents() == std::vector<std::string>{dir / "bin/wpa_supplicant"},
        "a program that cannot run counts as missing");
  ::chmod((dir / "bin/wpa_supplicant").c_str(), 0755);
  ::unlink((dir / "lib/modules/aic8800_fdrv.ko").c_str());
  ::mkdir((dir / "lib/modules/aic8800_fdrv.ko").c_str(), 0755);
  check(options.missingComponents() == std::vector<std::string>{dir / "lib/modules/aic8800_fdrv.ko"},
        "a directory named like a module does not count");
  ::rmdir((dir / "lib/modules/aic8800_fdrv.ko").c_str());
  ::unlink((dir / "bin/udhcpc").c_str());
  check(options.missingComponents() ==
            std::vector<std::string>{dir / "bin/udhcpc", dir / "lib/modules/aic8800_fdrv.ko"},
        "every missing component is named");
}

void reminder() {
  TempDir dir;
  Log::open(dir / "daemon.log");
  {
    EventLoop loop;
    LogReminder warning("adb-tcp", "WARNING: test reminder", 150);
    loop.add(warning, 0);
    const int64_t begin = posix::monotonicMs();
    check(loop.start() && warning.lines() == 1, "warning written at start");
    check(runUntil(loop, [&] { return warning.lines() >= 3; }, 2000), "warning repeated");
    check(posix::monotonicMs() - begin >= 300, "repeats keep their interval");
    loop.requestStop("test");
    runUntil(loop, [&] { return loop.finished(); }, 1000);
    runUntil(loop, [] { return false; }, 400);
    check(warning.lines() == 3, "no warning after the stop");
  }
  Log::close();
  check(occurrencesOf(readFile(dir / "daemon.log"), "adb-tcp: WARNING: test reminder") == 3, "one line per reminder");
}

void status() {
  DeviceState state;
  awtrix::tc002::PowerStatus power;
  power.usbPower = true;
  power.batteryPercent = 76;
  power.batteryMillivolts = 4120;
  state.setPower(power);
  DaemonInfo info;
  info.version = "1.2.3";
  const std::string text = daemonStatus(info, state, nullptr, nullptr, nullptr, 5000);
  check(awtrix::api::isWellFormed(text), "status is well-formed JSON");
  awtrix::api::JsonReader root(text);
  auto daemon = awtrix::api::memberValue(root, "daemon");
  std::string version;
  check(awtrix::api::memberValue(daemon, "version").appendString(version) && version == "1.2.3", "version reported");
  long long percent = 0;
  check(awtrix::api::memberValue(awtrix::api::memberValue(root, "power"), "batteryPercent").asLong(percent) &&
            percent == 76,
        "power reported");
  check(text.find("\"network\":{\"link\":\"unconfigured\"") != std::string::npos, "network block present");
  bool keepAdbTcp = true;
  check(awtrix::api::memberValue(daemon, "keepAdbTcp").asBool(keepAdbTcp) && !keepAdbTcp, "ADB TCP policy reported");
}

void stateLog() {
  TempDir dir;
  Log::open(dir / "daemon.log");
  DeviceState state;
  StateLog log(state);
  state.onPower([&] { log.power(); });
  state.onNetwork([&] { log.network(1000); });
  awtrix::tc002::PowerStatus power;
  power.usbPower = true;
  power.batteryPercent = 76;
  power.batteryMillivolts = 4120;
  state.setPower(power);
  power.batteryMillivolts = 4125;
  state.setPower(power);
  check(log.lines() == 1, "millivolt drift is not logged");
  power.batteryPercent = 75;
  state.setPower(power);
  check(log.lines() == 2, "percent change logged");

  awtrix::tc002::NetworkStatus network;
  network.link = awtrix::tc002::WifiLink::Connected;
  network.ssid = "Home";
  network.rssi = -60;
  network.ipv4 = "192.168.1.20";
  state.setNetwork(network);
  network.rssi = -61;
  state.setNetwork(network);
  check(log.lines() == 3, "RSSI drift is not logged");
  network.ipv4 = "192.168.1.21";
  state.setNetwork(network);
  check(log.lines() == 4, "address change logged");
  for (int i = 0; i < 40; ++i) {
    network.link = i % 2 ? awtrix::tc002::WifiLink::Connected : awtrix::tc002::WifiLink::Disconnected;
    state.setNetwork(network);
  }
  check(log.lines() == 2 + StateLog::kNetworkBurst, "network flapping is rate limited");
  Log::close();
  const std::string text = readFile(dir / "daemon.log");
  check(occurrencesOf(text, "ssid=\"Home\"") == 1, "SSID written only when it changed");
  check(text.find("logging paused") != std::string::npos, "suppression announced once");
}

}

int main() {
  tc002d_test::quietLogs();
  ordering();
  stopWaits();
  startFailure();
  orphans();
  boundedLog();
  options();
  developerFlag();
  releaseComponents();
  reminder();
  status();
  stateLog();
  return tc002d_test::finish("tc002d loop");
}
