// SpeakerBackend inside RuntimeChild: awtrix_pcm loaded in a forked child and its helper as the
// speaker, and every failure turning the speaker off until the next boot. argv[1] is the fake
// runtime, argv[2] the fake speaker helper.
#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>

#include <cerrno>
#include <climits>
#include <csignal>
#include <cstring>
#include <cstdlib>
#include <functional>
#include <memory>

#include "platform/tc002/daemon/RuntimeChild.h"
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

std::string fakeRuntime;
std::string fakeAudio;
const char kUnlisted[] =
    "aic8800_fdrv 667648 0 - Live 0xbf2e5000 (O)\naic8800_bsp 110592 1 aic8800_fdrv, Live 0xbf2c4000 (O)\n";
const char kListed[] =
    "aic8800_fdrv 667648 0 - Live 0xbf2e5000 (O)\nawtrix_pcm 16384 0 - Live 0xbf000000 (O)\n";

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
  bool childReady() const override { return true; }
  bool prepareChild(int64_t) override { return true; }
  int keysFd() const override { return keys_; }
  int knobFd() const override { return knob_; }

 private:
  int keys_ = -1, knob_ = -1;
  int writers_[2]{-1, -1};
};

struct Setup {
  bool wired = true;
  bool listed = false;
  bool modulesReadable = true;
  bool helper = true;
  bool module = true;
  unsigned moduleMode = 0644;
  std::string helperMode = "ok";
  std::string helperScript;
  // Runs in the forked loader; empty: lists awtrix_pcm and succeeds.
  std::function<int(const std::string& modules, int fd)> load;
  int64_t loadTimeoutMs = 3000;
  uid_t ownerOffset = 0;
};

struct Rig {
  TempDir dir;
  DeviceState state;
  FakeHardware hardware;
  RuntimeOptions options;
  std::unique_ptr<RuntimeChild> runtime;
  EventLoop loop;
  std::string data;
  std::string modules;
  std::string loaderMarker;

  explicit Rig(const Setup& setup) {
    posix::makeDirectories(dir / "root/bin", 0755);
    makeLink(fakeRuntime, dir / "root/bin/awtrix-linux");
    const std::string helper = dir / "root/bin/awtrix-tc002-audio-pcm";
    if (setup.helper && !setup.helperScript.empty()) {
      writeFile(helper, setup.helperScript);
      ::chmod(helper.c_str(), 0755);
    } else if (setup.helper) {
      makeLink(fakeAudio, helper);
      writeFile(dir / "root/bin/helper-mode", setup.helperMode);
    }
    const std::string module = dir / "root/lib/modules/awtrix_pcm.ko";
    if (setup.module) {
      writeFile(module, "\x7f" "ELF not really a module\n");
      ::chmod(module.c_str(), setup.moduleMode);
    }
    modules = dir / "proc/modules";
    if (setup.modulesReadable) writeFile(modules, setup.listed ? kListed : kUnlisted);
    writeFile(dir / "root/share/index.html", "<html></html>");
    data = dir / "data/app";
    posix::makeDirectories(data, 0700);
    writeFile(data + "/mode", "ok");
    loaderMarker = dir / "loader-called";
    Log::open(dir / "daemon.log");
    options.executable = dir / "root/bin/awtrix-linux";
    options.dataDir = data;
    options.webui = dir / "root/share/index.html";
    options.home = data;
    options.httpPort = 8080;
    options.readyTimeoutMs = 1500;
    options.healthyMs = 700;
    options.stopGraceMs = 400;
    options.prepareRetryMs = 20;
    options.backoffMs = {100, 200, 400};
    options.helperExitGraceMs = 300;
    if (setup.wired) {
      options.pcm.helper = helper;
      options.pcm.module = module;
      options.pcm.modules = modules;
    }
    options.pcmOwner = ::getuid() + setup.ownerOffset;
    options.moduleLoadTimeoutMs = setup.loadTimeoutMs;
    const std::string listing = modules, marker = loaderMarker;
    const auto load = setup.load;
    options.loadModule = [listing, marker, load](int fd) {
      writeFile(marker, "called\n");
      if (load) return load(listing, fd);
      return fd >= 0 && writeFile(listing, kListed) ? 0 : EIO;
    };
    runtime = std::make_unique<RuntimeChild>(state, hardware, options, RuntimeLinks{});
    loop.add(*runtime, 3000);
    loop.start();
  }
  ~Rig() {
    loop.requestStop("test end");
    runUntil(loop, [&] { return loop.finished(); }, 3000);
    Log::close();
  }
  RuntimeChild& rt() { return *runtime; }
  const SpeakerBackend& speaker() { return runtime->speaker(); }
  bool until(const std::function<bool()>& done, int timeoutMs) { return runUntil(loop, done, timeoutMs); }
  bool running() { return rt().state() == RuntimeChild::State::Running; }
  std::string audio(pid_t pid) { return readFile(data + "/audio-" + std::to_string(pid)); }
  std::string log() { return readFile(dir / "daemon.log"); }
  bool loaderCalled() { return exists(loaderMarker); }
  bool helperRan() { return exists(dir / "root/bin/helper.lock"); }
  std::string status() {
    std::string text;
    awtrix::api::JsonWriter json(text);
    json.beginObject();
    rt().appendStatus(json, posix::monotonicMs());
    json.endObject();
    return text;
  }
};

bool contains(const std::string& text, const std::string& needle) { return text.find(needle) != std::string::npos; }

void expectOff(Rig& rig, const std::string& what, const std::string& reason) {
  check(rig.until([&] { return rig.running(); }, 3000), what + ": runtime ready");
  check(rig.audio(rig.rt().pid()).empty() && rig.rt().helperStarts() == 0 && !rig.helperRan() &&
            rig.speaker().kind() == SpeakerBackend::Kind::Off && rig.rt().helperState() == "off",
        what + ": the speaker is off and the runtime runs without audio");
  check(contains(rig.speaker().note(), reason), what + ": reason recorded: " + rig.speaker().note());
  check(contains(rig.log(), "speaker backend: off until reboot"), what + ": logged");
  const std::string status = rig.status();
  check(contains(status, "\"state\":\"off\"") && contains(status, "\"backend\":\"off\"") &&
            contains(status, "\"backendNote\":\""),
        what + ": status " + status);
}

void unconfigured() {
  Setup setup;
  setup.wired = false;
  setup.listed = true;
  Rig rig(setup);
  check(rig.until([&] { return rig.running(); }, 3000), "no speaker configured: runtime ready");
  check(rig.audio(rig.rt().pid()).empty() && !rig.helperRan() && !rig.loaderCalled() &&
            rig.speaker().kind() == SpeakerBackend::Kind::None,
        "no speaker configured: no helper, no module load");
  check(!contains(rig.log(), "speaker backend"), "no speaker configured: nothing is logged");
}

void loadsModule() {
  Rig rig(Setup{});
  check(rig.until([&] { return rig.running(); }, 3000), "default: runtime ready");
  const pid_t first = rig.rt().pid();
  check(rig.loaderCalled() && readFile(rig.modules) == kListed, "default: the forked loader loaded awtrix_pcm");
  check(rig.audio(first) == "helper ok\n" && rig.helperRan(), "default: the awtrix_pcm helper got its launch contract");
  check(rig.speaker().kind() == SpeakerBackend::Kind::Pcm && contains(rig.speaker().note(), "loaded in"),
        "default: backend pcm: " + rig.speaker().note());
  check(contains(rig.log(), "speaker backend: awtrix_pcm helper " + rig.options.pcm.helper), "default: choice logged");
  const std::string status = rig.status();
  check(contains(status, "\"audio\":{\"helper\":\"" + rig.options.pcm.helper + "\",\"state\":\"running\"") &&
            contains(status, "\"backend\":\"pcm\"") && !contains(status, "developerFlag"),
        "default: status " + status);
  rig.rt().restart(posix::monotonicMs());
  check(rig.until([&] { return rig.running() && rig.rt().spawns() == 2; }, 4000), "default: runtime restarted");
  check(rig.audio(rig.rt().pid()) == "helper ok\n" && rig.rt().helperStarts() == 2 &&
            rig.rt().helperLastExit() == "exit 0 (clean)",
        "default: the next runtime gets a new helper after the old one exited");
}

void moduleAlreadyLoaded() {
  Setup setup;
  setup.listed = true;
  Rig rig(setup);
  check(rig.until([&] { return rig.running(); }, 3000), "already loaded: runtime ready");
  check(!rig.loaderCalled() && rig.helperRan() && rig.speaker().kind() == SpeakerBackend::Kind::Pcm &&
            contains(rig.speaker().note(), "already loaded"),
        "already loaded: no second load, the awtrix_pcm helper");
}

void failuresBeforeTheLoad() {
  {
    Setup setup;
    setup.helper = false;
    Rig rig(setup);
    expectOff(rig, "no helper", "unavailable");
    check(!rig.loaderCalled(), "no helper: the module is not loaded for nothing");
  }
  {
    Setup setup;
    setup.module = false;
    Rig rig(setup);
    expectOff(rig, "no module", "missing");
  }
  {
    Setup setup;
    setup.moduleMode = 0666;
    Rig rig(setup);
    expectOff(rig, "writable module", "missing or not a regular file");
    check(!rig.loaderCalled(), "a module others may write is never loaded");
  }
  {
    Setup setup;
    setup.ownerOffset = 1;
    Rig rig(setup);
    expectOff(rig, "foreign module", "missing or not a regular file");
    check(!rig.loaderCalled(), "a module of another owner is never loaded");
  }
  {
    Setup setup;
    setup.modulesReadable = false;
    Rig rig(setup);
    expectOff(rig, "unreadable module list", "cannot read");
    check(!rig.loaderCalled(), "an unknown module state never loads the module");
  }
}

void failuresOfTheLoad() {
  {
    Setup setup;
    setup.load = [](const std::string&, int) { return ENOEXEC; };
    Rig rig(setup);
    expectOff(rig, "load refused", std::strerror(ENOEXEC));
  }
  {
    Setup setup;
    setup.load = [](const std::string& modules, int) {
      writeFile(modules, kListed);
      return EIO;
    };
    Rig rig(setup);
    expectOff(rig, "load error with the module listed", "awtrix_pcm is listed");
  }
  {
    Setup setup;
    setup.loadTimeoutMs = 300;
    setup.load = [](const std::string&, int) {
      ::sleep(30);
      return 0;
    };
    Rig rig(setup);
    expectOff(rig, "hung load", "did not finish");
    check(rig.until([&] { return rig.speaker().loaderPid() < 0; }, 2000), "hung load: the loader is killed and reaped");
  }
}

void failuresOfTheHelper() {
  {
    Setup setup;
    setup.helperMode = "preflight";
    Rig rig(setup);
    check(rig.until([&] { return rig.running() && rig.rt().helperState() == "failed"; }, 3000),
          "helper refused: runtime ready");
    check(rig.rt().helperLastExit() == "exit 3 (preflight refused)" &&
              rig.speaker().kind() == SpeakerBackend::Kind::Off && contains(rig.log(), "off until reboot") &&
              contains(rig.speaker().note(), "exit 3"),
          "helper refused: the speaker is off: " + rig.rt().helperLastExit());
    rig.rt().restart(posix::monotonicMs());
    check(rig.until([&] { return rig.running() && rig.rt().spawns() == 2; }, 4000), "helper refused: runtime restarted");
    check(rig.rt().helperStarts() == 1 && rig.audio(rig.rt().pid()).empty() && rig.rt().helperState() == "off",
          "helper refused: no retry in this boot");
  }
  {
    Setup setup;
    setup.helperScript = "#!/bin/sh\nexit 4\n";
    Rig rig(setup);
    check(rig.until([&] { return rig.running() && rig.rt().helperState() == "failed"; }, 3000),
          "device missing: runtime ready");
    check(rig.rt().helperLastExit() == "exit 4 (device unavailable)" &&
              rig.speaker().kind() == SpeakerBackend::Kind::Off,
          "exit 4 reads as the device and turns the speaker off: " + rig.rt().helperLastExit());
  }
}

void stopWhileLoading() {
  Setup setup;
  setup.loadTimeoutMs = 10000;
  setup.load = [](const std::string&, int) {
    ::sleep(30);
    return 0;
  };
  pid_t loader = -1;
  {
    Rig rig(setup);
    check(rig.until([&] { return rig.speaker().loaderPid() > 0; }, 1000), "stop while loading: loader running");
    loader = rig.speaker().loaderPid();
    rig.until([] { return false; }, 100);
    check(rig.rt().spawns() == 0, "stop while loading: the runtime waits for the backend");
    rig.loop.requestStop("test");
    check(rig.until([&] { return rig.loop.finished(); }, 2000), "stop while loading: the daemon stops at once");
  }
  check(loader > 0 && ::kill(loader, 0) < 0 && errno == ESRCH, "stop while loading: the loader is killed and reaped");
}

}

int main(int argc, char** argv) {
  tc002d_test::quietLogs();
  if (argc < 3) {
    std::fprintf(stderr, "usage: %s FAKE_RUNTIME FAKE_AUDIO_HELPER\n", argv[0]);
    return 2;
  }
  char resolved[PATH_MAX];
  fakeRuntime = ::realpath(argv[1], resolved) ? resolved : argv[1];
  fakeAudio = ::realpath(argv[2], resolved) ? resolved : argv[2];
  unconfigured();
  loadsModule();
  moduleAlreadyLoaded();
  failuresBeforeTheLoad();
  failuresOfTheLoad();
  failuresOfTheHelper();
  stopWhileLoading();
  return tc002d_test::finish("tc002d speaker backend");
}
