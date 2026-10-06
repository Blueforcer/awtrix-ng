// Web update inside awtrix-tc002d: the hand-off from a supervised fake runtime, the copy of the
// flash helper and its command line, refusals that keep everything running, the boot-time
// reconciliation of what the flash helper did, the confirmation, the release slot's capacity, and a
// package from tools/update/package.py.
//   argv: AWTRIX_TC002D FAKE_RUNTIME PYTHON3 PACKAGE_PY
#include <fcntl.h>
#include <limits.h>
#include <sys/stat.h>
#include <sys/wait.h>

#include <cstdlib>
#include <cstring>
#include <memory>
#include <vector>

#include "platform/tc002/contract/SupervisorProtocol.h"
#include "platform/tc002/daemon/RuntimeChild.h"
#include "platform/tc002/daemon/update/Package.h"
#include "platform/tc002/daemon/update/ReleaseSlot.h"
#include "platform/tc002/daemon/update/UpdateRecord.h"
#include "platform/tc002/daemon/update/UpdateService.h"
#include "platform/posix/sha256.h"
#include "support.h"

using namespace awtrix::tc002d;
namespace tc002 = awtrix::tc002;
using tc002d_test::check;
using tc002d_test::exists;
using tc002d_test::makeLink;
using tc002d_test::makePipe;
using tc002d_test::readFile;
using tc002d_test::runUntil;
using tc002d_test::TempDir;
using tc002d_test::writeFile;

namespace {

std::string daemonBinary, fakeRuntime, python, packageTool;

constexpr uint64_t kCapacity = 5177344;
const char* const kFlashScript = "#!/bin/sh\nexit 0\n";

void be(std::string& out, uint64_t value, int bytes) {
  for (int i = bytes - 1; i >= 0; --i) out += static_cast<char>((value >> (8 * i)) & 0xff);
}

std::string sha256Hex(const std::string& data) {
  sha256_state state;
  sha256_init(&state);
  sha256_update(&state, data.data(), data.size());
  uint8_t digest[32];
  sha256_final(&state, digest);
  return posix::hexBytes(digest, sizeof digest);
}

std::string bytesOf(const std::string& hex) {
  std::string out;
  for (std::size_t i = 0; i + 1 < hex.size(); i += 2)
    out += static_cast<char>(std::stoi(hex.substr(i, 2), nullptr, 16));
  return out;
}

// A release image as far as the daemon looks: a squashfs superblock whose size is the image's.
std::string slotImage(std::size_t size, char fill) {
  std::string image(size, fill);
  std::memcpy(&image[0], "hsqs", 4);
  for (int i = 0; i < 8; ++i) image[40 + i] = static_cast<char>((static_cast<uint64_t>(size) >> (8 * i)) & 0xff);
  return image;
}

struct Release {
  std::string name;
  uint64_t counter = 0;
};

struct Package {
  std::string bytes;
  PackageHeader header;
};

Package makePackage(const Release& release, const std::string& payload, const std::string& target = "awtrix-ng:tc002") {
  const std::string payloadSha = sha256Hex(payload);
  std::string header = "AWUPD003";
  be(header, 3, 2);
  be(header, 0, 2);
  be(header, 68 + target.size() + release.name.size(), 4);
  be(header, payload.size(), 8);
  be(header, release.counter, 8);
  be(header, target.size(), 2);
  be(header, release.name.size(), 2);
  header += bytesOf(payloadSha) + target + release.name;
  const std::size_t manifestBytes = header.size();
  header += bytesOf(sha256Hex(header));
  Package package;
  package.bytes = header + payload;
  package.header.target = target;
  package.header.release = release.name;
  package.header.counter = release.counter;
  package.header.payloadSha256 = payloadSha;
  package.header.manifestBytes = manifestBytes;
  package.header.payloadOffset = manifestBytes + 32;
  package.header.payloadBytes = payload.size();
  return package;
}

bool writeMode(const std::string& path, const std::string& data, unsigned mode) {
  if (!writeFile(path, data)) return false;
  return ::chmod(path.c_str(), mode) == 0;
}

std::string manifestJson(const Release& release) {
  return "{\"release\": \"" + release.name + "\", \"counter\": " + std::to_string(release.counter) + ", \"files\": []}";
}

struct Fixture {
  TempDir dir;
  InstallPaths paths;
  std::string root, sys, package;
  Release old{"1.0.0-g1111", 100};
  Release next{"1.1.0-g2222", 200};

  Fixture() {
    paths.data = dir / "data";
    paths.updateDir = dir / "update";
    root = dir / "root";
    sys = dir / "sys";
    package = paths.updateDir + "/package.awup";
    posix::makeDirectories(paths.stateDir(), 0700);
    ::mkdir(paths.updateDir.c_str(), 0700);
    writeFile(root + "/manifest.json", manifestJson(old));
    writeMode(root + "/bin/awtrix-tc002-flash", kFlashScript, 0755);
  }
  tc002::UpdateStatus status() const { return UpdateRecord(paths.stateDir()).status(); }
  // The state main leaves before it execs the flash helper for next.
  void writing(const Package& package) {
    std::string error;
    UpdateRecord record(paths.stateDir(), old.counter);
    StagedUpdate staged;
    const bool pending = record.stage(package.header, this->package, error) && record.begin(staged, error) &&
                         record.bootPending(error);
    check(pending, "fixture: " + next.name + " is boot-pending (" + error + ")");
  }
  BootAction boot(const Release& running, const std::string& writerResult, std::string& candidate) {
    std::string note;
    return UpdateRecord(paths.stateDir(), running.counter)
        .reconcile({running.name, running.counter}, writerResult, candidate, note);
  }
};

void bootStates() {
  {
    Fixture f;
    f.writing(makePackage(f.next, slotImage(8192, 'n')));
    std::string candidate;
    check(f.boot(f.next, "done: " + f.next.name, candidate) == BootAction::Confirm && candidate == f.next.name &&
              f.status().state == "boot-pending",
          "boot: the new release runs and waits for its confirmation");
  }
  {
    Fixture f;
    f.writing(makePackage(f.next, slotImage(8192, 'n')));
    std::string candidate;
    check(f.boot(f.old, "source: the image is not a slot image", candidate) == BootAction::None, "boot: old runs");
    const tc002::UpdateStatus status = f.status();
    check(status.state == "failed" && status.release == f.next.name &&
              status.error.find("the release slot was not written (source: the image is not a slot image)") !=
                  std::string::npos,
          "boot: the flash helper's refusal becomes the failure (" + status.error + ")");
  }
  {
    Fixture f;
    f.writing(makePackage(f.next, slotImage(8192, 'n')));
    std::string candidate;
    check(f.boot(f.old, "", candidate) == BootAction::None &&
              f.status().error.find("release " + f.old.name + " runs instead") != std::string::npos,
          "boot: without a record from the flash helper the running release is named");
  }
  {
    Fixture f;
    f.writing(makePackage(f.next, slotImage(8192, 'n')));
    std::string candidate;
    check(f.boot({f.next.name, 999}, "done: " + f.next.name, candidate) == BootAction::None &&
              f.status().state == "failed",
          "boot: a release with the candidate's name but another counter is not the candidate");
  }
  {
    Fixture f;
    std::string error;
    check(UpdateRecord(f.paths.stateDir(), f.old.counter).stage(makePackage(f.next, slotImage(8192, 'n')).header,
                                                                 f.package, error),
          "boot: staged");
    std::string candidate;
    f.boot(f.old, "", candidate);
    check(f.status().state == "failed" && f.status().error.find("did not start") != std::string::npos,
          "boot: an update staged but never handed to the flash helper fails");
  }
  {
    Fixture f;
    {
      std::string error;
      UpdateRecord record(f.paths.stateDir(), f.old.counter);
      StagedUpdate staged;
      const bool activating = record.stage(makePackage(f.next, slotImage(8192, 'n')).header, f.package, error) &&
                              record.begin(staged, error);
      check(activating, "boot: activating (" + error + ")");
    }
    std::string candidate;
    f.boot(f.old, "", candidate);
    check(f.status().state == "failed" && f.status().error.find("interrupted") != std::string::npos,
          "boot: an interrupted activation fails");
  }
  {
    Fixture f;
    writeFile(f.paths.stateDir() + "/update-state.json", "{not json");
    std::string candidate;
    check(f.boot(f.old, "", candidate) == BootAction::None && exists(f.paths.stateDir() + "/update-state.json.corrupt") &&
              f.status().state == "idle",
          "boot: an unreadable state file is kept aside and started afresh");
  }
}

void slotCapacity() {
  TempDir dir;
  const std::string sys = dir / "sys";
  std::string error;
  check(releaseSlotCapacity(sys, error) == 0 && error.find("/proc/mtd") != std::string::npos,
        "slot: no /proc/mtd, no slot");
  writeFile(sys + "/proc/mtd", "dev:    size   erasesize  name\nmtd3: 00800000 00010000 \"res\"\n");
  check(releaseSlotCapacity(sys, error) == 0 && error.find("mtd3ro") != std::string::npos,
        "slot: an unreadable res device, no slot");
  writeFile(sys + "/dev/mtd/mtd3ro", slotImage(0x280000 + 100, 'r'));
  error.clear();
  check(releaseSlotCapacity(sys, error) == 0x800000 - 0x290000 - 0x10000 && error.empty(),
        "slot: the image starts one erase block after the header block behind the squashfs");
  writeFile(sys + "/dev/mtd/mtd3ro", std::string(4096, '\xff'));
  check(releaseSlotCapacity(sys, error) == 0 && error.find("no release slot") != std::string::npos,
        "slot: res without a squashfs has no slot");
  writeFile(sys + "/proc/mtd", "dev:    size   erasesize  name\nmtd6: 00800000 00010000 \"data\"\n");
  check(releaseSlotCapacity(sys, error) == 0 && error.find("no res partition") != std::string::npos,
        "slot: a table without res has no slot");
}

class FakeHardware : public ChildHardware {
 public:
  FakeHardware() {
    int keys[2], knob[2];
    makePipe(keys, O_CLOEXEC | O_NONBLOCK, "fake keys");
    makePipe(knob, O_CLOEXEC | O_NONBLOCK, "fake knob");
    fds_ = {keys[0], keys[1], knob[0], knob[1]};
  }
  ~FakeHardware() override {
    for (int fd : fds_) ::close(fd);
  }
  bool childReady() const override { return true; }
  bool prepareChild(int64_t) override { return true; }
  int keysFd() const override { return fds_[0]; }
  int knobFd() const override { return fds_[2]; }

 private:
  std::vector<int> fds_;
};

struct HandOffRig {
  bool mcuFirmwareBusy = false;
  Fixture f;
  std::string app;
  DeviceState state;
  FakeHardware hardware;
  EventLoop loop;
  std::unique_ptr<UpdateService> update;
  std::unique_ptr<RuntimeChild> runtime;

  explicit HandOffRig(uint64_t capacity = kCapacity) {
    app = f.dir / "app";
    ::mkdir(app.c_str(), 0700);
    makeLink(fakeRuntime, f.root + "/bin/awtrix-linux");
    writeFile(f.root + "/share/index.html", "<html></html>");
    UpdateServiceOptions options;
    options.paths = f.paths;
    options.root = f.root;
    options.capacity = capacity;
    options.running = readRunningRelease(f.root);
    UpdateLinks links;
    links.mcuFirmwareIdle = [this] { return !mcuFirmwareBusy; };
    links.stop = [this](const std::string& reason) { loop.requestStop(reason); };
    links.statusChanged = [this] {
      if (runtime) runtime->sendHello();
    };
    update = std::make_unique<UpdateService>(options, links);
    RuntimeOptions runtimeOptions;
    runtimeOptions.executable = f.root + "/bin/awtrix-linux";
    runtimeOptions.dataDir = app;
    runtimeOptions.webui = f.root + "/share/index.html";
    runtimeOptions.home = app;
    runtimeOptions.httpPort = 8080;
    runtimeOptions.readyTimeoutMs = 1500;
    runtimeOptions.healthyMs = 700;
    runtimeOptions.stopGraceMs = 400;
    runtimeOptions.prepareRetryMs = 20;
    runtimeOptions.caFile = f.root + "/share/ca-certificates.crt";
    runtimeOptions.updateState = f.paths.stateDir() + "/update-state.json";
    runtimeOptions.releaseRoot = f.root;
    runtimeOptions.updateDir = f.paths.updateDir;
    RuntimeLinks runtimeLinks;
    runtimeLinks.updateReady = [this](const tc002::UpdateReady& ready) { update->handOff(ready); };
    runtimeLinks.hello = [this] { return update->helloDatagram(); };
    runtime = std::make_unique<RuntimeChild>(state, hardware, runtimeOptions, runtimeLinks);
    loop.add(*runtime, 3000);
    loop.add(*update, 1000);
    loop.start();
  }
  ~HandOffRig() {
    loop.requestStop("test end");
    runUntil(loop, [&] { return loop.finished(); }, 3000);
  }
  void send(const std::string& line) {
    writeFile(app + "/outbox.tmp", line + "\n");
    ::rename((app + "/outbox.tmp").c_str(), (app + "/outbox").c_str());
  }
  std::string received() { return readFile(app + "/received.log"); }
  tc002::UpdateReady readyFor(const Package& package) const {
    tc002::UpdateReady ready;
    ready.package = f.package;
    ready.release = package.header.release;
    ready.counter = package.header.counter;
    ready.payloadSha256 = package.header.payloadSha256;
    return ready;
  }
};

void webIndex() {
  TempDir dir;
  DaemonOptions options;
  options.root = dir / "root";
  writeFile(options.root + "/share/index.html", "<html></html>");
  check(options.webIndexPath() == options.root + "/share/index.html", "web index: plain file without a .gz copy");
  writeFile(options.root + "/share/index.html.gz", "gz");
  check(options.webIndexPath() == options.root + "/share/index.html.gz", "web index: the .gz copy wins");
  check(options.caFilePath() == options.root + "/share/ca-certificates.crt", "web index: CA bundle lives in share/");
  check(options.flashHelperPath() == options.root + "/bin/awtrix-tc002-flash", "web index: the flash helper in bin/");
}

void handOff() {
  {
    HandOffRig rig;
    rig.mcuFirmwareBusy = true;
    tc002::UpdateReady ready;
    ready.release = rig.f.next.name;
    check(!rig.update->handOff(ready) && !rig.update->handOffPending() && !rig.loop.stopping() &&
              rig.update->status().error.find("MCU firmware update") != std::string::npos,
          "hand-off: MCU flash prevents Linux update handoff and shutdown");
  }
  {
    HandOffRig rig;
    const Package package = makePackage(rig.f.next, slotImage(12288, 'n'));
    writeMode(rig.f.package, package.bytes, 0600);
    check(runUntil(rig.loop, [&] { return rig.runtime->helloReceived(); }, 3000), "hand-off: runtime says hello");
    const std::string pid = std::to_string(rig.runtime->pid());
    check(readFile(rig.app + "/report-" + pid) == "valid\n" &&
              readFile(rig.app + "/update-args-" + pid) ==
                  "--ca-file " + rig.f.root + "/share/ca-certificates.crt --update-state " + rig.f.paths.stateDir() +
                      "/update-state.json --release-root " + rig.f.root + " --update-dir " + rig.f.paths.updateDir +
                      "\n",
          "hand-off: the runtime gets the CA bundle and the web update flags");
    check(runUntil(rig.loop, [&] { return rig.received().find("\"type\":\"hello\"") != std::string::npos; }, 2000),
          "hand-off: the supervisor answers with its hello");
    const std::string hello = rig.received();
    check(hello.find("\"capacity\":" + std::to_string(kCapacity)) != std::string::npos &&
              hello.find("\"factory\"") == std::string::npos &&
              hello.find("\"type\":\"hello\"") < hello.find("\"type\":\"power\""),
          "hand-off: the hello carries the update with the slot capacity and comes first");
    writeFile(rig.f.paths.updateResult(), "done: stale\n");
    rig.send(tc002::encodeUpdateReady(rig.readyFor(package)));
    check(runUntil(rig.loop, [&] { return rig.loop.finished(); }, 5000), "hand-off: the daemon stops everything");
    check(rig.update->handOffPending() && rig.f.status().state == "applying" &&
              rig.f.status().release == rig.f.next.name,
          "hand-off: the update is staged");
    check(rig.runtime->state() == RuntimeChild::State::Stopped, "hand-off: the runtime is stopped");
    const std::string helper = rig.f.paths.updateDir + "/awtrix-tc002-flash";
    struct stat info{};
    check(::stat(helper.c_str(), &info) == 0 && (info.st_mode & 07777) == 0700 && readFile(helper) == kFlashScript,
          "hand-off: the flash helper is copied into the private update directory");
    check(!exists(rig.f.paths.updateResult()), "hand-off: an old result of the flash helper goes");
    ExecPlan plan;
    std::string error;
    const bool prepared = rig.update->prepareExec(plan, error);
    const tc002::UpdateStatus pending = rig.f.status();
    check(prepared && pending.state == "boot-pending",
          "hand-off: the update is boot-pending before the exec (" + error + pending.error + ")");
    const std::vector<std::string> expected{
        helper, "write-slot", rig.f.package,
        "--offset", std::to_string(package.header.payloadOffset),
        "--length", "12288",
        "--sha256", package.header.payloadSha256,
        "--release", rig.f.next.name,
        "--counter", std::to_string(rig.f.next.counter),
        "--mount", rig.f.root,
        "--result", rig.f.paths.updateResult(),
        "--reboot"};
    check(plan.executable == helper && plan.arguments == expected, "hand-off: exec line of the flash helper");
    rig.update->execFailed("cannot start the flash helper: test");
    check(rig.f.status().state == "failed" && rig.f.status().error.find("cannot start the flash helper") !=
                                                  std::string::npos,
          "hand-off: a failed exec fails the update");
  }
  {
    HandOffRig rig;
    const Package package = makePackage(rig.f.next, slotImage(12288, 'n'));
    const std::string outside = rig.f.dir / "package.awup";
    writeMode(outside, package.bytes, 0600);
    check(runUntil(rig.loop, [&] { return rig.runtime->helloReceived(); }, 3000), "refusal: runtime says hello");
    tc002::UpdateReady ready = rig.readyFor(package);
    ready.package = outside;
    rig.send(tc002::encodeUpdateReady(ready));
    check(runUntil(rig.loop, [&] { return rig.received().find("refused") != std::string::npos; }, 3000),
          "refusal: a package outside the update directory is refused in a new hello");
    check(!rig.loop.stopping() && !rig.update->handOffPending() && rig.f.status().state == "idle",
          "refusal: nothing stops and nothing is staged");
    writeMode(rig.f.package, package.bytes, 0600);
    ready = rig.readyFor(package);
    ready.payloadSha256 = std::string(64, 'b');
    rig.send(tc002::encodeUpdateReady(ready));
    check(runUntil(rig.loop, [&] { return rig.update->status().error.find("does not match") != std::string::npos; },
                   3000),
          "refusal: a hand-off that disagrees with the package is refused");
    ::unlink((rig.f.root + "/bin/awtrix-tc002-flash").c_str());
    rig.send(tc002::encodeUpdateReady(rig.readyFor(package)));
    check(runUntil(rig.loop, [&] { return rig.update->status().error.find("no flash helper") != std::string::npos; },
                   3000),
          "refusal: without the flash helper nothing is handed over");
    check(!rig.loop.stopping() && rig.f.status().state == "idle", "refusal: the runtime keeps running");
  }
  {
    HandOffRig rig(8192);
    const Package package = makePackage(rig.f.next, slotImage(12288, 'n'));
    writeMode(rig.f.package, package.bytes, 0600);
    check(runUntil(rig.loop, [&] { return rig.runtime->helloReceived(); }, 3000), "capacity: runtime says hello");
    rig.send(tc002::encodeUpdateReady(rig.readyFor(package)));
    check(runUntil(rig.loop, [&] { return rig.update->status().error.find("does not fit") != std::string::npos; }, 3000),
          "capacity: an image larger than the slot is refused");
    check(!rig.loop.stopping() && rig.f.status().state == "idle", "capacity: nothing stops");
  }
  {
    HandOffRig rig(0);
    const Package package = makePackage(rig.f.next, slotImage(12288, 'n'));
    writeMode(rig.f.package, package.bytes, 0600);
    check(!rig.update->handOff(rig.readyFor(package)) &&
              rig.update->status().error.find("no release slot") != std::string::npos,
          "capacity: without a release slot nothing is handed over");
  }
}

struct ConfirmRig {
  Fixture f;
  EventLoop loop;
  bool healthy = false;
  int statusChanges = 0;
  std::unique_ptr<UpdateService> update;

  ConfirmRig() {
    f.writing(makePackage(f.next, slotImage(8192, 'n')));
    writeFile(f.root + "/manifest.json", manifestJson(f.next));
    std::string candidate;
    check(f.boot(f.next, "done: " + f.next.name, candidate) == BootAction::Confirm, "confirm rig: boot-pending");
    UpdateServiceOptions options;
    options.paths = f.paths;
    options.root = f.root;
    options.capacity = kCapacity;
    options.running = readRunningRelease(f.root);
    options.confirmPending = true;
    options.candidate = candidate;
    options.bootAttemptsPaths = counters();
    options.checkIntervalMs = 50;
    for (const auto& path : options.bootAttemptsPaths) writeFile(path, "1\n");
    UpdateLinks links;
    links.runtimeHealthy = [this] { return healthy; };
    links.statusChanged = [this] { ++statusChanges; };
    update = std::make_unique<UpdateService>(options, links);
    loop.add(*update, 1000);
    loop.start();
  }
  ~ConfirmRig() {
    loop.requestStop("test end");
    runUntil(loop, [&] { return loop.finished(); }, 2000);
  }
  std::vector<std::string> counters() const {
    return {f.paths.stateDir() + "/boot-attempts", f.sys + "/tmp/awtrix-loader.attempts"};
  }
  bool countersKept() const {
    for (const auto& path : counters())
      if (!exists(path)) return false;
    return true;
  }
  bool countersCleared() const {
    for (const auto& path : counters())
      if (exists(path)) return false;
    return true;
  }
};

void confirmation() {
  ConfirmRig rig;
  runUntil(rig.loop, [] { return false; }, 300);
  check(rig.update->confirming() && rig.f.status().state == "boot-pending" && rig.countersKept(),
        "confirm: nothing happens before the runtime is healthy, the start counters stay");
  tc002::UpdateReady ready;
  ready.release = "x";
  check(!rig.update->handOff(ready) && rig.update->status().error.find("not confirmed") != std::string::npos,
        "confirm: no new update while the release waits for its confirmation");
  rig.healthy = true;
  check(runUntil(rig.loop, [&] { return !rig.update->confirming(); }, 1000) &&
            rig.f.status().state == "confirmed" && rig.statusChanges >= 1,
        "confirm: a healthy runtime confirms");
  check(rig.update->status().state == "confirmed" && rig.countersCleared(),
        "confirm: the next hello says confirmed and the loader start counters are cleared");
}

int runProcess(const std::vector<std::string>& args, int timeoutMs) {
  const pid_t pid = ::fork();
  if (pid == 0) {
    const int null = ::open("/dev/null", O_RDWR);
    ::dup2(null, STDIN_FILENO);
    if (!std::getenv("TC002D_TEST_VERBOSE")) {
      ::dup2(null, STDOUT_FILENO);
      ::dup2(null, STDERR_FILENO);
    }
    std::vector<char*> argv;
    for (const auto& arg : args) argv.push_back(const_cast<char*>(arg.c_str()));
    argv.push_back(nullptr);
    ::execv(argv[0], argv.data());
    _exit(127);
  }
  const int64_t deadline = posix::monotonicMs() + timeoutMs;
  int status = 0;
  while (::waitpid(pid, &status, WNOHANG) == 0) {
    if (posix::monotonicMs() >= deadline) {
      ::kill(pid, SIGKILL);
      ::waitpid(pid, &status, 0);
      return -1;
    }
    ::usleep(10000);
  }
  return WIFEXITED(status) ? WEXITSTATUS(status) : -1;
}

void removedOptions() {
  check(runProcess({daemonBinary, "--apply-update", "/p", "--root", "/r", "--data", "/d"}, 5000) == 2 &&
            runProcess({daemonBinary, "--factory", "no-install", "--root", "/r", "--data", "/d", "--boot"}, 5000) == 2,
        "binary: --apply-update and --factory are usage errors");
}

void packageTool_() {
  Fixture f;
  const std::string image = f.dir / "release.img", awup = f.dir / "awtrix-ng-tc002.awup";
  writeFile(image, slotImage(16384, 'i'));
  if (runProcess({python, packageTool, "--image", image, "--output", awup, "--release", f.next.name, "--counter",
                  std::to_string(f.next.counter)},
                 30000) != 0) {
    check(false, "package.py: builds an update package from an image");
    return;
  }
  const std::string bytes = readFile(awup);
  writeMode(f.package, bytes, 0600);
  posix::UniqueFd fd(::open(f.package.c_str(), O_RDONLY | O_CLOEXEC));
  PackageHeader header;
  std::string error;
  check(readPackageHeader(fd.get(), bytes.size(), header, error) && header.release == f.next.name &&
            header.counter == f.next.counter && header.payloadBytes == 16384 &&
            header.payloadSha256 == sha256Hex(readFile(image)) &&
            bytes.substr(header.payloadOffset) == readFile(image),
        "package.py: the header reads back and the payload is the image (" + error + ")");
  writeFile(image, std::string(16384, 'x'));
  check(runProcess({python, packageTool, "--image", image, "--output", awup + ".2", "--release", f.next.name,
                    "--counter", "1"},
                   30000) != 0 &&
            !exists(awup + ".2"),
        "package.py: an image that is no squashfs is refused");
}

}

int main(int argc, char** argv) {
  tc002d_test::quietLogs();
  if (argc < 5) {
    std::fprintf(stderr, "usage: %s AWTRIX_TC002D FAKE_RUNTIME PYTHON3 PACKAGE_PY\n", argv[0]);
    return 2;
  }
  char resolved[PATH_MAX];
  daemonBinary = ::realpath(argv[1], resolved) ? resolved : argv[1];
  fakeRuntime = ::realpath(argv[2], resolved) ? resolved : argv[2];
  python = argv[3];
  packageTool = argv[4];
  ::umask(022);
  bootStates();
  slotCapacity();
  webIndex();
  handOff();
  confirmation();
  removedOptions();
  packageTool_();
  return tc002d_test::finish("tc002d update");
}
