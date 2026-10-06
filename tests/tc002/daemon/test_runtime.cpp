// RuntimeChild supervising the fake runtime (argv[1]) with shortened timings.
#include <fcntl.h>
#include <sys/ptrace.h>
#include <sys/wait.h>

#include <atomic>
#include <cerrno>
#include <climits>
#include <csignal>
#include <cstdlib>
#include <functional>
#include <memory>
#include <thread>
#include <vector>

#include "platform/tc002/daemon/RuntimeChild.h"
#include "platform/tc002/daemon/ForwardedLog.h"
#include "../../ManualClock.h"
#include "support.h"

using namespace awtrix::tc002d;
using tc002d_test::check;
using tc002d_test::exists;
using tc002d_test::makeLink;
using tc002d_test::makePipe;
using tc002d_test::readFile;
using tc002d_test::runUntil;
using tc002d_test::TempDir;
using tc002d_test::writeFile;

namespace {

std::string fakeBinary;
std::string fakeAudio;

class FakeHardware : public ChildHardware {
 public:
  FakeHardware() {
    int keys[2], knob[2];
    makePipe(keys, O_CLOEXEC | O_NONBLOCK, "fake keys");
    makePipe(knob, O_CLOEXEC | O_NONBLOCK, "fake knob");
    keys_ = keys[0];
    knob_ = knob[0];
    writers_[0] = keys[1];
    writers_[1] = knob[1];
  }
  ~FakeHardware() override {
    for (int fd : {keys_, knob_, writers_[0], writers_[1]}) ::close(fd);
  }
  bool childReady() const override { return ready; }
  bool prepareChild(int64_t) override {
    ++prepares;
    return prepareOk;
  }
  int keysFd() const override { return keys_; }
  int knobFd() const override { return knob_; }

  bool ready = true;
  bool prepareOk = true;
  unsigned prepares = 0;

 private:
  int keys_ = -1, knob_ = -1;
  int writers_[2]{-1, -1};
};

class FakeControls : public WifiControl, public TimeControl, public HostnameControl {
 public:
  bool setCredentials(const awtrix::tc002::WifiCredentials& credentials) override {
    calls.push_back("wifi " + credentials.ssid + " " + credentials.password);
    return true;
  }
  void eraseCredentials() override { calls.push_back("erase"); }
  void scan(ScanDone done) override {
    calls.push_back("scan");
    pendingScan = std::move(done);
  }
  void setServer(const std::string& server) override { calls.push_back("ntp " + server); }
  void setHostname(const std::string& hostname) override { calls.push_back("hostname " + hostname); }
  std::vector<std::string> calls;
  ScanDone pendingScan;
};

std::size_t occurrences(const std::string& text, const std::string& needle) {
  std::size_t count = 0;
  for (auto at = text.find(needle); at != std::string::npos; at = text.find(needle, at + 1)) ++count;
  return count;
}

struct Rig {
  TempDir dir;
  std::string data;
  DeviceState state;
  FakeHardware hardware;
  FakeControls controls;
  bool reboot = false;
  std::vector<bool> bluetoothRequests;
  RuntimeLinks::PcmReply pendingPcm;
  std::vector<int> pcmRequests;
  RuntimeLinks::StreamReply streamReply;
  std::vector<std::pair<int, bool>> streamControls;
  RuntimeOptions options;
  std::unique_ptr<RuntimeChild> runtime;
  EventLoop loop;

  explicit Rig(const std::string& mode, const std::function<void(RuntimeOptions&)>& tweak = {},
               bool withWifi = true, const std::string& helperMode = "") {
    posix::makeDirectories(dir / "root/bin", 0755);
    makeLink(fakeBinary, dir / "root/bin/awtrix-linux");
    if (!helperMode.empty() && helperMode != "missing") {
      makeLink(fakeAudio, dir / "root/bin/awtrix-tc002-audio-pcm");
      writeFile(dir / "root/bin/helper-mode", helperMode);
    }
    if (!helperMode.empty()) {
      writeFile(dir / "proc/modules", "awtrix_pcm 16384 0 - Live 0xbf000000 (O)\n");
      options.pcm.helper = dir / "root/bin/awtrix-tc002-audio-pcm";
      options.pcm.modules = dir / "proc/modules";
    }
    writeFile(dir / "root/share/index.html", "<html></html>");
    data = dir / "data/app";
    posix::makeDirectories(data, 0700);
    setMode(mode);
    writeFile(dir / "data/state/boot-attempts", "2\n");
    writeFile(dir / "tmp/awtrix-loader.attempts", "1\n");
    Log::open(dir / "daemon.log");
    options.executable = dir / "root/bin/awtrix-linux";
    options.dataDir = data;
    options.webui = dir / "root/share/index.html";
    options.home = data;
    options.bootAttemptsPaths = {dir / "data/state/boot-attempts", dir / "tmp/awtrix-loader.attempts"};
    options.httpPort = 8080;
    options.readyTimeoutMs = 1500;
    options.healthyMs = 700;
    options.stopGraceMs = 400;
    options.prepareRetryMs = 20;
    options.backoffMs = {100, 200, 400, 800, 1600};
    if (tweak) tweak(options);
    RuntimeLinks links;
    links.microphoneStreamAvailable = [] { return true; };
    links.microphoneStreamStart = [this](int, RuntimeLinks::StreamReply reply) {
      streamReply = std::move(reply);
      return true;
    };
    links.microphoneStreamControl = [this](int epoch, bool stop) {
      streamControls.emplace_back(epoch, stop);
    };
    links.microphonePcm = [this](int id, RuntimeLinks::PcmReply reply) {
      pcmRequests.push_back(id);
      pendingPcm = std::move(reply);
    };
    links.bluetooth = [this](bool on) { bluetoothRequests.push_back(on); };
    links.wifi = withWifi ? &controls : nullptr;
    links.time = &controls;
    links.hostname = &controls;
    links.reboot = [this] {
      reboot = true;
      loop.requestStop("reboot requested by the runtime");
    };
    runtime = std::make_unique<RuntimeChild>(state, hardware, options, links);
    loop.add(*runtime, 3000);
    loop.start();
  }
  ~Rig() {
    loop.requestStop("test end");
    runUntil(loop, [&] { return loop.finished(); }, 3000);
    Log::close();
  }
  RuntimeChild& rt() { return *runtime; }
  void setMode(const std::string& mode) { writeFile(data + "/mode", mode); }
  std::string report(pid_t pid) { return readFile(data + "/report-" + std::to_string(pid)); }
  std::string received() { return readFile(data + "/received.log"); }
  void send(const std::string& lines) {
    writeFile(data + "/outbox.tmp", lines);
    ::rename((data + "/outbox.tmp").c_str(), (data + "/outbox").c_str());
  }
  bool until(const std::function<bool()>& done, int timeoutMs) { return runUntil(loop, done, timeoutMs); }
  void advance(int64_t milliseconds) {
    awtrix_test::advanceClock(milliseconds);
    loop.runOnce(0);
  }
  bool running() { return rt().state() == RuntimeChild::State::Running; }
  std::string audio(pid_t pid) { return readFile(data + "/audio-" + std::to_string(pid)); }
  std::string intro(pid_t pid) { return readFile(data + "/intro-" + std::to_string(pid)); }
  std::string voice(pid_t pid) { return readFile(data + "/voice-" + std::to_string(pid)); }
  // The start reason and the --uid of every runtime so far, oldest first.
  std::vector<std::string> starts() { return lines("starts"); }
  std::vector<std::string> uids() { return lines("uids"); }
  std::vector<std::string> lines(const std::string& name) {
    std::vector<std::string> lines;
    const std::string text = readFile(data + "/" + name);
    for (std::size_t at = 0, end; at < text.size(); at = end + 1) {
      end = text.find('\n', at);
      if (end == std::string::npos) end = text.size();
      lines.push_back(text.substr(at, end - at));
    }
    return lines;
  }
  void publishMac(const std::string& mac) {
    awtrix::tc002::NetworkStatus network = state.network();
    network.mac = mac;
    state.setNetwork(network);
  }
  bool countersKept() { return exists(options.bootAttemptsPaths[0]) && exists(options.bootAttemptsPaths[1]); }
  bool countersCleared() { return !exists(options.bootAttemptsPaths[0]) && !exists(options.bootAttemptsPaths[1]); }
};

bool exitedUnreaped(pid_t pid, int timeoutMs) {
  const int64_t deadline = posix::monotonicMs() + timeoutMs;
  for (;;) {
    siginfo_t info{};
    if (::waitid(P_PID, static_cast<id_t>(pid), &info, WEXITED | WNOHANG | WNOWAIT) == 0 && info.si_pid == pid)
      return true;
    if (posix::monotonicMs() >= deadline) return false;
    ::usleep(5000);
  }
}

// A second process ptrace-attaches to a child of this one and never waits for it. Once the child
// dies only the tracer could reap it, which is how a task stuck in the kernel looks to its parent.
class Tracer {
 public:
  explicit Tracer(pid_t target) {
    int ack[2];
    if (::pipe2(ack, O_CLOEXEC) < 0) return;
    pid_ = ::fork();
    if (pid_ == 0) {
      ::signal(SIGCHLD, SIG_DFL);
      for (int fd = 3; fd < 1024; ++fd)
        if (fd != ack[1]) ::close(fd);
      const char result = ::ptrace(PTRACE_SEIZE, target, nullptr, nullptr) == 0 ? 'y' : 'n';
      (void)!::write(ack[1], &result, 1);
      for (;;) ::pause();
    }
    ::close(ack[1]);
    char result = 'n';
    attached_ = pid_ > 0 && ::read(ack[0], &result, 1) == 1 && result == 'y';
    ::close(ack[0]);
  }
  ~Tracer() { release(); }
  bool attached() const { return attached_; }
  pid_t pid() const { return pid_; }
  // The tracee's exit reaches this process once the tracer is gone.
  void release() {
    if (pid_ <= 0) return;
    ::kill(pid_, SIGKILL);
    while (::waitpid(pid_, nullptr, 0) < 0 && errno == EINTR) {}
    pid_ = -1;
  }

 private:
  pid_t pid_ = -1;
  bool attached_ = false;
};

// False when body had to be rescued by ending the tracer after rescueMs.
bool finishesWithin(Tracer& tracer, int rescueMs, const std::function<void()>& body) {
  std::atomic<bool> done{false}, rescued{false};
  std::thread watchdog([&] {
    const int64_t deadline = posix::monotonicMs() + rescueMs;
    while (!done && posix::monotonicMs() < deadline) ::usleep(10000);
    if (!done) {
      rescued = true;
      ::kill(tracer.pid(), SIGKILL);
    }
  });
  body();
  done = true;
  watchdog.join();
  return !rescued;
}

void unreapableChildren() {
  {
    Rig rig("ok");
    check(rig.until([&] { return rig.running(); }, 3000), "runtime ready before it becomes unreapable");
    const pid_t pid = rig.rt().pid();
    Tracer tracer(pid);
    if (!tracer.attached()) {
      std::printf("unreapable-child checks skipped: ptrace is not permitted here\n");
      return;
    }
    rig.loop.requestStop("test");
    rig.loop.runOnce(0);
    rig.advance(rig.options.stopGraceMs);
    rig.advance(3000);
    check(rig.until([&] { return rig.loop.finished(); }, 5000) && rig.rt().pid() == pid,
          "the stop sequence gives up on a runtime that cannot be reaped");
    const int64_t begin = posix::monotonicMs();
    check(finishesWithin(tracer, 3000, [&] { rig.runtime.reset(); }) && posix::monotonicMs() - begin < 500,
          "the destructor leaves an unreapable runtime that was already sent SIGKILL to init");
    tracer.release();
    ::waitpid(pid, nullptr, 0);
  }
  {
    Rig rig("ok", [](RuntimeOptions& options) { options.helperExitGraceMs = 100; }, true, "ok");
    check(rig.until([&] { return rig.running() && rig.rt().helperPid() > 0; }, 3000), "runtime with helper ready");
    const pid_t helper = rig.rt().helperPid();
    Tracer tracer(helper);
    check(tracer.attached(), "tracer holds the helper");
    rig.loop.requestStop("test");
    check(rig.until([&] { return rig.rt().pid() < 0; }, 1000), "runtime reaped before helper deadline");
    rig.advance(3000);
    check(rig.until([&] { return rig.loop.finished(); }, 5000) && rig.rt().helperPid() == helper,
          "the stop sequence gives up on a helper that cannot be reaped");
    const int64_t begin = posix::monotonicMs();
    check(finishesWithin(tracer, 4000, [&] { rig.runtime.reset(); }) && posix::monotonicMs() - begin < 2000,
          "the destructor waits only briefly for a helper that cannot be reaped");
    tracer.release();
    ::waitpid(helper, nullptr, 0);
  }
}

void bootCounter() {
  const auto slowHealth = [](RuntimeOptions& options) { options.healthyMs = 60000; };
  {
    Rig rig("ok", slowHealth);
    check(rig.until([&] { return rig.running() && rig.rt().helloReceived(); }, 3000), "runtime ready");
    check(rig.countersKept(), "boot counters kept before the healthy period");
    rig.send("{\"v\":2,\"type\":\"reboot\"}\n");
    check(rig.until([&] { return rig.loop.finished(); }, 4000) && rig.reboot, "reboot requested");
    check(rig.countersCleared(), "a reboot the ready runtime asked for is not a failed boot");
  }
  {
    Rig rig("ok", slowHealth);
    check(rig.until([&] { return rig.running(); }, 3000), "runtime ready");
    rig.loop.requestStop("test");
    check(rig.until([&] { return rig.loop.finished(); }, 2000), "daemon stopped");
    check(rig.countersCleared(), "a stop with the runtime ready is not a failed boot");
  }
  for (const std::string mode : {"silent", "crash"}) {
    Rig rig(mode, slowHealth);
    if (mode == "silent") {
      check(rig.until([&] { return rig.report(rig.rt().pid()) == "valid\n"; }, 1000), "silent runtime launched");
      rig.advance(rig.options.readyTimeoutMs);
    }
    check(rig.until([&] { return rig.rt().spawns() >= 2; }, 3000), mode + " runtime restarted");
    rig.loop.requestStop("test");
    check(rig.until([&] { return rig.loop.finished(); }, 3000), "daemon stopped");
    check(rig.countersKept(), "a stop while the runtime is " + mode + " keeps the boot counters");
  }
}

void requestsBeforeExit() {
  Rig rig("abrupt", [](RuntimeOptions& options) { options.healthyMs = 60000; });
  check(rig.until([&] { return rig.running() && rig.rt().helloReceived(); }, 3000), "abrupt runtime ready");
  const pid_t pid = rig.rt().pid();
  rig.send("{\"v\":2,\"type\":\"factoryReset\"}\n{\"v\":2,\"type\":\"reboot\"}\n");
  check(exitedUnreaped(pid, 3000), "runtime exited right after sending its requests");
  check(rig.until([&] { return rig.loop.finished(); }, 3000) && rig.reboot,
        "a reboot request read together with the runtime's exit is honoured");
  check(rig.controls.calls == std::vector<std::string>({"erase"}),
        "a factory reset sent just before the exit erases the credentials");
  check(rig.countersCleared(), "the ready runtime's reboot cleared the boot counters");
}

void speakerHelper() {
  {
    Rig rig("ok", [](RuntimeOptions& options) { options.helperExitGraceMs = 1500; }, true, "slow");
    check(rig.until([&] { return rig.running(); }, 3000), "runtime with speaker helper ready");
    const pid_t first = rig.rt().pid();
    check(rig.report(first) == "valid\n", "launch contract with fd 104: " + rig.report(first));
    check(rig.audio(first) == "helper ok\n", "helper got an empty environment and fd 104: " + rig.audio(first));
    check(rig.rt().helperPid() > 0 && rig.rt().helperState() == "running" && rig.rt().helperStarts() == 1,
          "helper supervised");
    rig.rt().restart(posix::monotonicMs());
    check(rig.until([&] { return rig.running() && rig.rt().spawns() == 2; }, 4000), "runtime restarted");
    const pid_t second = rig.rt().pid();
    check(rig.audio(second) == "helper ok\n", "new helper started only after the old one was reaped: " + rig.audio(second));
    check(rig.rt().helperStarts() == 2 && rig.rt().helperLastExit() == "exit 0 (clean)",
          "previous helper exited cleanly: " + rig.rt().helperLastExit());
    std::string status;
    awtrix::api::JsonWriter json(status);
    json.beginObject();
    rig.rt().appendStatus(json, posix::monotonicMs());
    json.endObject();
    check(status.find("\"audio\":{\"helper\":\"" + rig.options.pcm.helper + "\",\"state\":\"running\"") !=
                  std::string::npos &&
              status.find("\"lastExit\":\"exit 0 (clean)\"") != std::string::npos,
          "helper state in status: " + status);
    rig.loop.requestStop("test");
    check(rig.until([&] { return rig.loop.finished(); }, 4000) && rig.rt().helperPid() < 0, "stop reaps the helper");
  }
  {
    Rig rig("ok", {}, true, "preflight");
    check(rig.until([&] { return rig.running(); }, 3000), "runtime starts although the helper refused");
    check(rig.until([&] { return rig.rt().helperState() == "failed"; }, 2000) &&
              rig.rt().helperLastExit() == "exit 3 (preflight refused)",
          "helper preflight failure recorded: " + rig.rt().helperLastExit());
  }
  {
    Rig rig("ok", {}, true, "missing");
    check(rig.until([&] { return rig.running(); }, 3000), "runtime starts without a helper binary");
    check(rig.report(rig.rt().pid()) == "valid\n" && rig.rt().helperState() == "off" && rig.rt().helperStarts() == 0,
          "a missing helper turns the speaker off, so there is no audio fd: " + rig.report(rig.rt().pid()));
  }
  {
    Rig rig("ok", [](RuntimeOptions& options) { options.helperExitGraceMs = 200; }, true, "linger");
    check(rig.until([&] { return rig.running(); }, 3000), "runtime with lingering helper ready");
    rig.rt().restart(posix::monotonicMs());
    check(rig.until([&] { return rig.rt().pid() < 0; }, 1000), "runtime exits while its helper lingers");
    rig.advance(rig.options.helperExitGraceMs);
    rig.advance(60000);
    check(rig.until([&] { return rig.running() && rig.rt().spawns() == 2; }, 6000), "restart waits for the kill");
    check(rig.rt().helperLastExit() == "signal 9 (killed)", "SIGTERM ignored, SIGKILL reaped the helper: " +
                                                               rig.rt().helperLastExit());
    check(rig.report(rig.rt().pid()) == "valid\n" && rig.audio(rig.rt().pid()).empty() &&
              rig.rt().helperStarts() == 1 && rig.rt().helperState() == "off",
          "a helper that had to be killed failed, so the new runtime runs without audio");
  }
}

void bootIntro() {
  const auto introOn = [](IntroStarts starts, const std::string& sound) {
    return [starts, sound](RuntimeOptions& options) {
      options.introStarts = starts;
      options.bootSound = sound;
    };
  };
  {
    Rig rig("ok", introOn(IntroStarts::First, "/release/share/boot.mp3"), true, "ok");
    check(rig.until([&] { return rig.running(); }, 3000), "first runtime ready");
    const pid_t first = rig.rt().pid();
    check(rig.report(first) == "valid\n", "intro arguments follow the launch contract: " + rig.report(first));
    check(rig.intro(first) == "intro /release/share/boot.mp3\n", "the first start shows the intro with its sound: " +
                                                                      rig.intro(first));
    check(rig.rt().introGiven() && !rig.rt().introOnNextStart(), "the first start used the intro up");
    std::string status;
    awtrix::api::JsonWriter json(status);
    json.beginObject();
    rig.rt().appendStatus(json, posix::monotonicMs());
    json.endObject();
    check(status.find("\"bootIntro\":true") != std::string::npos, "status names the intro: " + status);
    rig.rt().restart(posix::monotonicMs());
    check(rig.until([&] { return rig.running() && rig.rt().spawns() == 2; }, 4000), "operator restart");
    check(rig.intro(rig.rt().pid()) == "none\n" && !rig.rt().introGiven(), "a restart shows no intro");
    rig.setMode("crash");
    rig.rt().restart(posix::monotonicMs());
    check(rig.until([&] { return rig.rt().spawns() >= 4; }, 4000), "crash restarts");
    rig.setMode("ok");
    check(rig.until([&] { return rig.running(); }, 4000), "recovered");
    check(rig.intro(rig.rt().pid()) == "none\n", "neither does a start after a crash");
  }
  {
    Rig rig("crash", introOn(IntroStarts::First, ""));
    check(rig.until([&] { return rig.rt().spawns() >= 2; }, 3000), "first start crashed");
    rig.setMode("ok");
    check(rig.until([&] { return rig.running(); }, 3000), "second start ready");
    check(rig.intro(rig.rt().pid()) == "none\n", "a crashed first start used the intro up");
    const std::string log = readFile(rig.dir / "daemon.log");
    check(log.find("(start 1) with the boot intro\n") != std::string::npos, "the log names the intro start: " + log);
  }
  {
    Rig rig("ok", introOn(IntroStarts::Every, ""));
    check(rig.until([&] { return rig.running(); }, 3000), "ready");
    check(rig.intro(rig.rt().pid()) == "intro\n", "every start: without a sound the intro comes alone");
    rig.rt().restart(posix::monotonicMs());
    check(rig.until([&] { return rig.running() && rig.rt().spawns() == 2; }, 4000), "restarted");
    check(rig.intro(rig.rt().pid()) == "intro\n" && rig.rt().introGiven(), "every start shows the intro");
  }
  {
    Rig rig("ok", introOn(IntroStarts::None, "/release/share/boot.mp3"));
    check(rig.until([&] { return rig.running(); }, 3000), "ready");
    check(rig.intro(rig.rt().pid()) == "none\n" && !rig.rt().introGiven(), "no intro when it is off");
  }
}

void speechVoice() {
  const auto withVoice = [](RuntimeOptions& options) { options.speechVoice = "/release/share/speech/voice.atts"; };
  {
    Rig rig("ok", withVoice, true, "ok");
    check(rig.until([&] { return rig.running(); }, 3000), "runtime with speaker and voice ready");
    const pid_t pid = rig.rt().pid();
    check(rig.report(pid) == "valid\n" && rig.voice(pid) == "/release/share/speech/voice.atts\n",
          "the voice comes with the speaker: " + rig.report(pid) + rig.voice(pid));
  }
  {
    Rig rig("ok", withVoice, true, "missing");
    check(rig.until([&] { return rig.running(); }, 3000), "runtime without speaker ready");
    const pid_t pid = rig.rt().pid();
    check(rig.report(pid) == "valid\n" && rig.voice(pid) == "none\n", "no speaker, no voice: " + rig.voice(pid));
  }
}

void healthyRun() {
  Rig rig("ok");
  check(rig.until([&] { return rig.running(); }, 3000), "runtime reports readiness");
  const pid_t pid = rig.rt().pid();
  const std::string report = rig.report(pid);
  check(report == "valid\n", "launch contract (arguments, descriptors, environment, session): " + report);
  check(rig.until([&] { return rig.rt().helloReceived(); }, 2000) && rig.rt().runtimeVersion() == "fake-1", "hello");
  check(rig.until([&] {
          const std::string got = rig.received();
          return got.find("\"type\":\"power\"") != std::string::npos &&
                 got.find("\"type\":\"network\"") != std::string::npos &&
                 got.find("\"type\":\"time\"") != std::string::npos;
        }, 2000),
        "full snapshot after hello");

  awtrix::tc002::PowerStatus power;
  power.usbPower = true;
  power.batteryPercent = 50;
  power.batteryMillivolts = 3900;
  rig.state.setPower(power);
  awtrix::tc002::NetworkStatus network;
  network.link = awtrix::tc002::WifiLink::Connected;
  network.ssid = "Home";
  network.ipv4 = "192.168.1.20";
  rig.state.setNetwork(network);
  check(rig.until([&] {
          const std::string got = rig.received();
          return got.find("\"batteryPercent\":50") != std::string::npos &&
                 got.find("\"ipv4\":\"192.168.1.20\"") != std::string::npos;
        }, 2000),
        "state changes forwarded");

  check(rig.until([&] { return rig.countersCleared(); }, 2000), "both boot counters cleared once healthy");
  check(rig.rt().consecutiveFailures() == 0, "no failures");

  rig.send("{\"v\":2,\"type\":\"wifi\",\"ssid\":\"Home\",\"password\":\"hunter22\"}\n"
           "{\"v\":2,\"type\":\"ntp\",\"server\":\"time.example\"}\n"
           "not json\n"
           "{\"v\":2,\"type\":\"power\",\"usbPower\":true,\"batteryPercent\":1,\"batteryMillivolts\":1}\n"
           "{\"v\":2,\"type\":\"hostname\",\"name\":\"clock\"}\n"
           "{\"v\":2,\"type\":\"ntp\",\"server\":\"time.example\"}\n"
           "{\"v\":2,\"type\":\"hostname\",\"name\":\"clock\"}\n"
           "{\"v\":2,\"type\":\"hostname\",\"name\":\"clock2\"}\n"
           "{\"v\":2,\"type\":\"wifiScan\"}\n"
           "{\"v\":2,\"type\":\"factoryReset\"}\n");
  check(rig.until([&] { return rig.controls.calls.size() >= 6; }, 2000), "runtime commands dispatched");
  rig.until([] { return false; }, 100);
  check(rig.controls.calls == std::vector<std::string>({"wifi Home hunter22", "ntp time.example", "hostname clock",
                                                        "hostname clock2", "scan", "erase"}),
        "wifi, ntp, hostname, scan and factory reset dispatched in order; repeated values are idempotent");
  check(rig.running() && rig.state.power().batteryPercent == 50, "malformed and wrong-direction messages ignored");
  check(static_cast<bool>(rig.controls.pendingScan), "scan handed to the Wi-Fi service");
  if (rig.controls.pendingScan) rig.controls.pendingScan({{"Cafe", -48, true}, {"Open", -70, false}});
  check(rig.until([&] {
          return rig.received().find("\"type\":\"wifiScanResult\",\"networks\":[{\"ssid\":\"Cafe\",\"rssi\":-48,"
                                     "\"secure\":true},{\"ssid\":\"Open\",\"rssi\":-70,\"secure\":false}]") !=
                 std::string::npos;
        }, 2000),
        "scan result delivered to the runtime");
  rig.send("{\"v\":2,\"type\":\"wifiScan\"}\n");
  check(rig.until([&] { return rig.controls.calls.size() == 7 && rig.controls.calls.back() == "scan"; }, 2000),
        "second scan requested");
  const auto staleScan = rig.controls.pendingScan;

  std::string status;
  awtrix::api::JsonWriter json(status);
  json.beginObject();
  rig.rt().appendStatus(json, posix::monotonicMs());
  json.endObject();
  check(status.find("\"state\":\"running\"") != std::string::npos && status.find("\"version\":\"fake-1\"") != std::string::npos,
        "status block: " + status);

  rig.rt().restart(posix::monotonicMs());
  check(rig.until([&] { return rig.rt().spawns() == 2 && rig.running(); }, 3000), "restart starts a new runtime");
  check(rig.rt().pid() != pid && rig.rt().lastExit() == "exit 0" && rig.rt().consecutiveFailures() == 0,
        "operator restart is not a failure");
  check(rig.until([&] { return rig.rt().helloReceived(); }, 2000), "new runtime said hello");
  const std::size_t results = occurrences(rig.received(), "wifiScanResult");
  if (staleScan) staleScan({{"Late", -60, true}});
  rig.until([] { return false; }, 200);
  check(occurrences(rig.received(), "wifiScanResult") == results, "a scan finishing after a restart is dropped");

  const std::string log = readFile(rig.dir / "daemon.log");
  check(log.find("awtrix-linux: fake runtime pid " + std::to_string(pid) + " mode ok") != std::string::npos,
        "runtime output captured in the daemon log");

  const pid_t second = rig.rt().pid();
  rig.send("{\"v\":2,\"type\":\"reboot\"}\n");
  check(rig.until([&] { return rig.loop.finished(); }, 4000) && rig.reboot, "reboot request stops the daemon");
  check(rig.rt().stopped() && rig.rt().pid() < 0 && rig.rt().lastExit() == "exit 0", "runtime reaped before reboot");
  check(readFile(rig.data + "/exit-" + std::to_string(second)) == "self\n",
        "the runtime was allowed to exit on its own after asking for a reboot");
}

void plainStop() {
  Rig rig("ok", {}, false);
  check(rig.until([&] { return rig.running() && rig.rt().helloReceived(); }, 3000), "running");
  const pid_t pid = rig.rt().pid();
  rig.send("{\"v\":2,\"type\":\"wifi\",\"ssid\":\"Home\",\"password\":\"hunter22\"}\n{\"v\":2,\"type\":\"factoryReset\"}\n"
           "{\"v\":2,\"type\":\"wifiScan\"}\n");
  check(rig.until([&] { return rig.received().find("\"type\":\"wifiScanResult\",\"networks\":[]") != std::string::npos; },
                  2000),
        "without a Wi-Fi service a scan is answered with an empty list");
  check(rig.running() && rig.controls.calls.empty(), "credentials and factory reset without a Wi-Fi service are harmless");
  rig.loop.requestStop("test");
  check(rig.until([&] { return rig.loop.finished(); }, 2000) && rig.rt().stopped() && rig.rt().pid() < 0,
        "stop terminates the runtime");
  check(readFile(rig.data + "/exit-" + std::to_string(pid)) == "term\n", "plain stop uses SIGTERM right away");
}

void crashBackoff() {
  Rig rig("crash");
  std::vector<int64_t> starts;
  unsigned seen = 0;
  rig.until([&] {
    if (rig.rt().spawns() != seen) {
      seen = rig.rt().spawns();
      starts.push_back(posix::monotonicMs());
    }
    return seen >= 4;
  }, 4000);
  check(starts.size() == 4, "crashing runtime restarted repeatedly");
  for (std::size_t i = 1; i < starts.size() && i <= 3; ++i) {
    const int64_t gap = starts[i] - starts[i - 1];
    check(gap >= rig.options.backoffMs[i - 1],
          "backoff step " + std::to_string(i) + " was " + std::to_string(gap) + " ms");
  }
  check(rig.rt().lastExit() == "exit 1" && rig.rt().consecutiveFailures() >= 3, "failures counted");
  check(rig.countersKept(), "boot counters kept while failing");
  rig.setMode("ok");
  check(rig.until([&] { return rig.running(); }, 4000), "recovers once the runtime works again");
  check(rig.until([&] { return rig.rt().consecutiveFailures() == 0; }, 2000), "backoff resets after a healthy period");
}

void readinessTimeout() {
  Rig rig("silent");
  check(rig.until([&] { return rig.report(rig.rt().pid()) == "valid\n"; }, 1000), "silent runtime launched");
  rig.advance(rig.options.readyTimeoutMs);
  check(rig.until([&] { return rig.rt().spawns() >= 2; }, 4000), "runtime without readiness is restarted");
  check(rig.rt().lastExit() == "exit 0" && rig.rt().consecutiveFailures() >= 1, "missing readiness counts as failure");
}

void stubbornChild() {
  Rig rig("stubborn", [](RuntimeOptions& options) { options.readyTimeoutMs = 300; });
  check(rig.until([&] { return rig.rt().spawns() >= 2; }, 3000), "restarted after SIGKILL");
  check(rig.rt().lastExit() == "signal 9", "SIGTERM ignored, SIGKILL after the grace period");
}

void startReasons() {
  {
    Rig rig("ok", [](RuntimeOptions& options) { options.firstStartReason = awtrix::tc002::kStartSoftware; });
    check(rig.until([&] { return rig.running(); }, 3000), "first runtime ready");
    rig.rt().restart(posix::monotonicMs());
    check(rig.until([&] { return rig.starts().size() == 2 && rig.running(); }, 3000), "restarted on request");
    rig.setMode("crash");
    rig.rt().restart(posix::monotonicMs());
    check(rig.until([&] { return rig.starts().size() >= 4; }, 3000), "a crashing runtime is started again");
    const std::vector<std::string> starts = rig.starts();
    check(starts.size() >= 4 && starts[0] == "software" && starts[1] == "software" && starts[2] == "software" &&
              starts[3] == "panic",
          "the daemon's reason, then software after a requested restart and panic after a crash");
  }
  {
    Rig rig("silent");
    check(rig.until([&] { return rig.report(rig.rt().pid()) == "valid\n"; }, 1000), "silent runtime launched");
    rig.advance(rig.options.readyTimeoutMs);
    check(rig.until([&] { return rig.starts().size() >= 2; }, 5000), "a silent runtime is replaced");
    const std::vector<std::string> starts = rig.starts();
    check(starts.size() >= 2 && starts[0] == "poweron" && starts[1] == "watchdog",
          "power-on by default, watchdog after the readiness deadline");
  }
  {
    Rig rig("closed");
    check(rig.until([&] { return rig.starts().size() >= 2; }, 5000), "a start that fails is repeated");
    const std::vector<std::string> starts = rig.starts();
    check(starts.size() >= 2 && starts[1] == "panic", "a runtime that closes its channel and fails counts as panic");
  }
}

// The device id kept in data/state/uid (stored when not empty), waited for at most waitMs.
std::function<void(RuntimeOptions&)> keptUid(const std::string& stored, int64_t waitMs) {
  return [stored, waitMs](RuntimeOptions& options) {
    options.uidPath = posix::parentDirectory(options.bootAttemptsPaths[0]) + "/uid";
    options.uidWaitMs = waitMs;
    if (!stored.empty()) writeFile(options.uidPath, stored + "\n");
  };
}

void deviceId() {
  {
    Rig rig("ok");
    check(rig.until([&] { return rig.running(); }, 3000), "runtime ready without a device id file");
    rig.publishMac("AA:BB:CC:00:11:22");
    rig.until([] { return false; }, 200);
    check(rig.uids() == std::vector<std::string>{"none"} && rig.rt().spawns() == 1,
          "no --uid and no restart without a place to keep the id");
  }
  {
    Rig rig("ok", keptUid("", 5000));
    rig.until([] { return false; }, 300);
    check(rig.rt().spawns() == 0, "the first start waits for the Wi-Fi MAC while no id is kept");
    rig.publishMac("AA:BB:CC:00:11:22");
    check(rig.until([&] { return rig.running(); }, 1000), "the MAC ends the wait at once");
    check(rig.uids() == std::vector<std::string>{"aabbcc001122"} &&
              readFile(rig.options.uidPath) == "aabbcc001122\n",
          "the runtime gets the MAC's id, and the id is kept");
  }
  {
    Rig rig("ok", keptUid("aabbcc001122", 5000));
    check(rig.until([&] { return rig.running(); }, 1000), "a kept id starts the runtime without waiting");
    rig.publishMac("aa:bb:cc:00:11:22");
    rig.until([] { return false; }, 200);
    check(rig.uids() == std::vector<std::string>{"aabbcc001122"} && rig.rt().spawns() == 1,
          "the same MAC changes nothing");
  }
  {
    Rig rig("ok", keptUid("0123456789ab", 5000));
    check(rig.until([&] { return rig.running(); }, 1000), "runtime ready with the kept id");
    rig.publishMac("AA:BB:CC:00:11:22");
    check(rig.until([&] { return rig.rt().spawns() == 2 && rig.running(); }, 3000),
          "a runtime with another id is restarted");
    rig.publishMac("AA:BB:CC:00:11:33");
    rig.until([] { return false; }, 300);
    check(rig.uids() == std::vector<std::string>{"0123456789ab", "aabbcc001122"} &&
              rig.starts().back() == "software" && rig.rt().spawns() == 2 &&
              readFile(rig.options.uidPath) == "aabbcc001133\n",
          "restarted once as software; a later id is kept for the next start");
  }
  {
    Rig rig("ok", keptUid("", 300));
    check(rig.until([&] { return rig.running(); }, 2000), "the runtime starts once the wait ran out");
    rig.publishMac("AA:BB:CC:00:11:22");
    check(rig.until([&] { return rig.rt().spawns() == 2 && rig.running(); }, 3000),
          "and is restarted with the id once the MAC is known");
    check(rig.uids() == std::vector<std::string>{"none", "aabbcc001122"}, "without an id first, then with it");
  }
  {
    Rig rig("ok", keptUid("not-an-id", 300));
    rig.until([] { return false; }, 150);
    check(rig.rt().spawns() == 0, "a malformed kept id counts as none");
  }
}

void forwardedLog() {
  Rig rig("ok");
  Log::forwardTo([&rig](const char* component, std::string_view text) { rig.rt().logLine(component, text); });
  for (int i = 0; i < 20; ++i) Log::line("wifi", "retry %d", i);
  Log::line("lease", "input devices held");
  check(rig.until([&] { return rig.running(); }, 3000), "runtime ready");
  const auto has = [&](const std::string& text) { return rig.received().find(text) != std::string::npos; };
  check(rig.until([&] { return has("\"component\":\"wifi\",\"text\":\"retry 19\""); }, 2000),
        "lines from before the hello follow it");
  check(!has("\"text\":\"retry 0\"") &&
            occurrences(rig.received(), "\"component\":\"wifi\"") <= RuntimeLog::kBacklog,
        "only the last lines from before the hello are kept");
  check(!has("input devices held"), "components the web UI log does not show stay in daemon.log");
  Log::line("update", "installing 1.2.3");
  check(rig.until([&] { return has("\"component\":\"update\",\"text\":\"installing 1.2.3\""); }, 2000),
        "a line reaches the running runtime at once");
  check(!has("fake runtime pid"), "the runtime's own output never comes back to it");
  Log::forwardTo(nullptr);
}

void exitLinesReachTheNextRuntime() {
  Rig rig("ok");
  Log::forwardTo([&rig](const char* component, std::string_view text) { rig.rt().logLine(component, text); });
  const auto has = [&](const std::string& text) { return rig.received().find(text) != std::string::npos; };
  check(rig.until([&] { return rig.running(); }, 3000), "runtime ready");
  const pid_t first = rig.rt().pid();
  rig.rt().restart(posix::monotonicMs());
  check(rig.until([&] { return rig.rt().spawns() == 2 && rig.running(); }, 3000), "restarted on request");
  check(rig.until([&] { return has("stopping pid " + std::to_string(first) + ": restart requested") &&
                               has("pid " + std::to_string(first) + " ended"); }, 2000),
        "the next runtime hears why and how the one before ended");
  const pid_t second = rig.rt().pid();
  ::kill(second, SIGKILL);
  check(rig.until([&] { return rig.rt().spawns() == 3 && rig.running(); }, 3000), "started again after a crash");
  check(rig.until([&] { return has("pid " + std::to_string(second) + " ended"); }, 2000),
        "the next runtime hears about the crash");
  Log::forwardTo(nullptr);
}

void earlyLinesReachTheFirstRuntime() {
  ForwardedLog forwarded;
  Log::line("update", "boot-pending release 1.2.3 failed: the new release did not start");
  Log::line("lease", "input devices held");
  Rig rig("ok");
  const ForwardedLog::Attachment attachment = forwarded.attach(rig.rt());
  const auto has = [&](const std::string& text) { return rig.received().find(text) != std::string::npos; };
  check(rig.until([&] { return has("boot-pending release 1.2.3 failed"); }, 3000),
        "the update notes of the daemon's start reach the first runtime");
  check(!has("input devices held"), "only the components the web UI log shows");
}

void wrongReadiness() {
  Rig rig("badready");
  check(rig.until([&] { return rig.rt().spawns() >= 2; }, 3000), "invalid readiness record rejected");
  check(rig.rt().consecutiveFailures() >= 1, "counted as failure");
}

void bluetoothLifecycle() {
  {
    Rig rig("abrupt");
    check(rig.until([&] { return rig.running() && rig.rt().helloReceived(); }, 3000), "runtime ready for final Bluetooth request");
    const pid_t pid = rig.rt().pid();
    rig.send(awtrix::tc002::encodeBluetooth({true, ""}) + "\n");
    check(exitedUnreaped(pid, 3000), "runtime exited with its last request queued");
    rig.hardware.ready = false;
    check(rig.until([&] { return !rig.rt().lastExit().empty(); }, 3000), "runtime exit reaped");
    check(rig.bluetoothRequests == std::vector<bool>({true, false}),
          "final Bluetooth request is drained before the controller is released");
  }
  {
    Rig rig("ok");
    const std::string first = awtrix::tc002::encodeBluetooth({false, "initial"});
    rig.rt().bluetoothStatus({false, "initial"});
    check(rig.until([&] { return rig.running() && rig.received().find(first) != std::string::npos; }, 3000),
          "Bluetooth state published before hello reaches the runtime");
    const pid_t pid = rig.rt().pid();
    check(::kill(pid, SIGSTOP) == 0, "pause runtime consumption");
    int status = 0;
    check(::waitpid(pid, &status, WUNTRACED) == pid && WIFSTOPPED(status), "runtime stopped reading");
    for (int i = 0; i < 2000; ++i) rig.state.setPower({});
    rig.rt().bluetoothStatus({false, "superseded"});
    rig.rt().bluetoothStatus({true, ""});
    check(::kill(pid, SIGCONT) == 0, "resume runtime consumption");
    const std::string latest = awtrix::tc002::encodeBluetooth({true, ""});
    check(rig.until([&] { return rig.received().find(latest) != std::string::npos; }, 3000),
          "resync delivers the latest Bluetooth state after backpressure");
    check(rig.received().find("superseded") == std::string::npos, "resync replaces the intermediate Bluetooth state");
  }
}

void microphoneLifecycle() {
  using namespace awtrix::tc002;
  {
    Rig rig("ok");
    check(rig.until([&] { return rig.running(); }, 3000), "PCM: runtime ready");
    rig.send(encodeMicrophonePcmRequest(7) + "\n");
    check(rig.until([&] { return !!rig.pendingPcm; }, 3000), "PCM: capture requested");
    const pid_t pid = rig.rt().pid();
    check(::kill(pid, SIGSTOP) == 0, "PCM: pause runtime consumption");
    int status = 0;
    check(::waitpid(pid, &status, WUNTRACED) == pid && WIFSTOPPED(status), "PCM: runtime stopped reading");
    for (int i = 0; i < 2000; ++i) rig.state.setPower({});
    MicrophonePcm pcm{7, std::vector<int16_t>(kMicrophonePcmSamples, -1234), {}};
    const auto expected = encodeMicrophonePcm(pcm);
    auto complete = std::move(rig.pendingPcm);
    complete(pcm);
    check(::kill(pid, SIGCONT) == 0, "PCM: resume runtime consumption");
    check(rig.until([&] { return rig.received().find(expected) != std::string::npos; }, 3000),
          "PCM: nonreplaceable completion survives supervisor backpressure");
    rig.until([] { return false; }, 120);
    check(occurrences(rig.received(), expected) == 1 && rig.pcmRequests == std::vector<int>{7},
          "PCM: completion delivered once without repeating the capture");
  }
  for (const bool completedBeforeExit : {false, true}) {
    Rig rig("ok");
    check(rig.until([&] { return rig.running(); }, 3000), "PCM generation: runtime ready");
    rig.send(encodeMicrophonePcmRequest(8) + "\n");
    check(rig.until([&] { return !!rig.pendingPcm; }, 3000), "PCM generation: capture requested");
    const pid_t pid = rig.rt().pid();
    check(::kill(pid, SIGSTOP) == 0, "PCM generation: pause child");
    int status = 0;
    check(::waitpid(pid, &status, WUNTRACED) == pid && WIFSTOPPED(status), "PCM generation: child paused");
    for (int i = 0; i < 2000; ++i) rig.state.setPower({});
    auto complete = std::move(rig.pendingPcm);
    MicrophonePcm old{8, {}, "old capture"};
    if (completedBeforeExit) complete(old);
    check(::kill(pid, SIGKILL) == 0, "PCM generation: kill stalled child");
    check(rig.until([&] { return rig.running() && rig.rt().pid() != pid; }, 3000),
          "PCM generation: replacement child running");
    if (!completedBeforeExit) complete(old);
    rig.send(encodeMicrophonePcmRequest(9) + "\n");
    check(rig.until([&] { return !!rig.pendingPcm; }, 3000), "PCM generation: new capture can start");
    complete = std::move(rig.pendingPcm);
    MicrophonePcm current{9, {}, "new capture"};
    complete(current);
    check(rig.until([&] { return rig.received().find(encodeMicrophonePcm(current)) != std::string::npos; }, 3000),
          "PCM generation: new result delivered");
    check(rig.received().find("old capture") == std::string::npos,
          "PCM generation: queued or late completion never reaches another child");
  }
}

void microphoneStreamLifecycle() {
  using namespace awtrix::tc002;
  {
    Rig rig("ok");
    check(rig.until([&] { return rig.running(); }, 3000), "stream: runtime ready");
    rig.send(encodeStreamControl({7, StreamControl::Start}) + "\n");
    check(rig.until([&] { return !!rig.streamReply; }, 3000), "stream: capture requested");
    auto reply = rig.streamReply;
    const pid_t pid = rig.rt().pid();
    check(::kill(pid, SIGSTOP) == 0, "stream: pause consumer");
    int status = 0;
    check(::waitpid(pid, &status, WUNTRACED) == pid && WIFSTOPPED(status), "stream: consumer paused");
    for (int i = 0; i < 2000; ++i) rig.state.setPower({});
    std::vector<std::string> expected;
    for (unsigned i = 0; i < 8; ++i) {
      StreamEvent event{7, StreamEvent::Kind::Audio, i * 480, 0, 100, false,
                        std::vector<int16_t>(480, static_cast<int16_t>(i - 100)), {}};
      check(reply(event), "stream: eight bounded blocks accepted under backpressure");
      expected.push_back(encodeStreamEvent(event));
    }
    check(!reply({7, StreamEvent::Kind::Audio, 3840, 0, 100, false, std::vector<int16_t>(480), {}}),
          "stream: ninth audio block reports backpressure instead of dropping accepted speech");
    StreamEvent ended{7, StreamEvent::Kind::Ended, 3840, 0, 100, false, {}, "consumer stalled"};
    check(reply(ended), "stream: terminal reserved even at audio queue limit");
    expected.push_back(encodeStreamEvent(ended));
    check(::kill(pid, SIGCONT) == 0, "stream: resume consumer");
    check(rig.until([&] { return rig.received().find(expected.back()) != std::string::npos; }, 3000),
          "stream: terminal delivered");
    const auto received = rig.received();
    std::size_t position = 0;
    for (const auto& event : expected) {
      const auto found = received.find(event, position);
      check(found != std::string::npos && occurrences(received, event) == 1,
            "stream: accepted audio and terminal preserved in order exactly once");
      position = found + event.size();
    }
  }
  {
    Rig rig("ok");
    check(rig.until([&] { return rig.running(); }, 3000), "stream generation: ready");
    rig.send(encodeStreamControl({8, StreamControl::Start}) + "\n");
    check(rig.until([&] { return !!rig.streamReply; }, 3000), "stream generation: started");
    auto old = std::move(rig.streamReply);
    const pid_t pid = rig.rt().pid();
    check(::kill(pid, SIGKILL) == 0, "stream generation: runtime exits");
    check(rig.until([&] { return rig.running() && rig.rt().pid() != pid; }, 3000), "stream generation: restarted");
    check(std::find(rig.streamControls.begin(), rig.streamControls.end(), std::make_pair(8, true)) != rig.streamControls.end(),
          "stream generation: exit revokes microphone lease");
    rig.send(encodeStreamControl({8, StreamControl::Start}) + "\n");
    check(rig.until([&] { return !!rig.streamReply; }, 3000), "stream generation: same epoch in new runtime");
    StreamEvent event{8, StreamEvent::Kind::Started, 0, 0, 100, false, {}, {}};
    check(!old(event), "stream generation: late old callback rejected even with reused epoch");
    check(rig.streamReply(event), "stream generation: new callback accepted");
    check(rig.until([&] { return rig.received().find(encodeStreamEvent(event)) != std::string::npos; }, 3000),
          "stream generation: new runtime receives its own capture");
  }
}

void hardwareGate() {
  {
    Rig gated("ok");
    gated.hardware.ready = false;
    gated.until([] { return false; }, 200);
    check(gated.rt().spawns() == 0 && gated.hardware.prepares == 0, "no start before the lease holds the inputs");
    gated.hardware.ready = true;
    check(gated.until([&] { return gated.running(); }, 3000), "starts once the lease holds the inputs");
  }
  {
    Rig blocked("ok");
    blocked.hardware.prepareOk = false;
    blocked.until([] { return false; }, 200);
    check(blocked.rt().spawns() == 0 && blocked.hardware.prepares >= 3, "prepare retried until the lease agrees");
    blocked.hardware.prepareOk = true;
    check(blocked.until([&] { return blocked.running(); }, 3000), "starts once the lease agrees");
  }
}

}

int main(int argc, char** argv) {
  tc002d_test::quietLogs();
  if (argc < 3) {
    std::fprintf(stderr, "usage: %s FAKE_RUNTIME FAKE_AUDIO_HELPER\n", argv[0]);
    return 2;
  }
  char resolved[PATH_MAX];
  fakeBinary = ::realpath(argv[1], resolved) ? resolved : argv[1];
  fakeAudio = ::realpath(argv[2], resolved) ? resolved : argv[2];
  healthyRun();
  startReasons();
  deviceId();
  forwardedLog();
  exitLinesReachTheNextRuntime();
  earlyLinesReachTheFirstRuntime();
  plainStop();
  crashBackoff();
  readinessTimeout();
  stubbornChild();
  wrongReadiness();
  bluetoothLifecycle();
  microphoneLifecycle();
  microphoneStreamLifecycle();
  hardwareGate();
  speakerHelper();
  speechVoice();
  bootIntro();
  bootCounter();
  requestsBeforeExit();
  unreapableChildren();
  return tc002d_test::finish("tc002d runtime");
}
