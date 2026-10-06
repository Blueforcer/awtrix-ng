#include "platform/tc002/daemon/Process.h"
#include "platform/posix/Time.h"
#include <dirent.h>
#include <fcntl.h>
#include <sys/file.h>
#include <sys/prctl.h>
#include <sys/reboot.h>
#include <sys/stat.h>
#include <unistd.h>

#include <cerrno>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <memory>
#include <string>
#include <vector>

#include "platform/tc002/daemon/ControlSocket.h"
#include "platform/tc002/daemon/EventLoop.h"
#include "platform/posix/Files.h"
#include "platform/tc002/daemon/HardwareLease.h"
#include "platform/tc002/daemon/Kernel.h"
#include "platform/tc002/daemon/Log.h"
#include "platform/tc002/daemon/Autostart.h"
#include "platform/tc002/daemon/BtService.h"
#include "platform/tc002/daemon/McuService.h"
#include "platform/tc002/daemon/Options.h"
#include "platform/tc002/daemon/PropertyWorkspace.h"
#include "platform/tc002/daemon/RuntimeChild.h"
#include "platform/tc002/daemon/ForwardedLog.h"
#include "platform/tc002/daemon/StartReason.h"
#include "platform/tc002/daemon/Status.h"
#include "platform/tc002/daemon/StockApp.h"
#include "platform/tc002/daemon/ip/IpService.h"
#include "platform/tc002/daemon/update/ReleaseSlot.h"
#include "platform/tc002/daemon/update/UpdatePaths.h"
#include "platform/tc002/daemon/update/UpdateRecord.h"
#include "platform/tc002/daemon/update/UpdateService.h"
#include "platform/tc002/daemon/wifi/WifiService.h"

using namespace awtrix::tc002d;
namespace posix = awtrix::posix;

namespace {

constexpr int kExitOk = 0, kExitFailure = 1, kExitUsage = 2, kExitStart = 3;
constexpr int64_t kAdbWarningIntervalMs = 3600000;

void sanitizeDescriptors() {
  for (int fd = 0; fd <= 2; ++fd) {
    if (::fcntl(fd, F_GETFD) >= 0) continue;
    const int null = ::open("/dev/null", O_RDWR);
    if (null >= 0 && null != fd) {
      ::dup2(null, fd);
      ::close(null);
    }
  }
  const int workspace = PropertyWorkspace::fd();
  closeOthers(&workspace, 1);
}

// Records the reboot (StartReason.h) and reboots. With a fake system tree (--sys-root, tests) the
// reboot itself is only recorded.
int rebootNow(const DaemonOptions& options) {
  markReboot(options);
  ::sync();
  const std::string& sysRoot = options.sysRoot;
  if (!sysRoot.empty()) {
    const std::string marker = sysRoot + "/tmp/awtrix-tc002d.reboot";
    posix::UniqueFd fd(::open(marker.c_str(), O_WRONLY | O_CREAT | O_TRUNC | O_CLOEXEC | O_NOFOLLOW, 0600));
    Log::line("daemon", "reboot %s %s", fd.valid() ? "recorded in" : "cannot be recorded in", marker.c_str());
    return kExitOk;
  }
  ::reboot(RB_AUTOBOOT);
  Log::line("daemon", "reboot failed: %s", std::strerror(errno));
  return kExitFailure;
}

int runCtl(int argc, char** argv) {
  CtlOptions options;
  std::string error;
  if (!parseCtlOptions(argc, argv, options, error)) {
    std::fprintf(stderr, "awtrix-tc002d ctl: %s\n%s", error.c_str(), usageText());
    return kExitUsage;
  }
  std::string request = options.command;
  if (options.payloadFromStdin) {
    std::string payload;
    char buffer[1024];
    ssize_t count;
    while ((count = ::read(STDIN_FILENO, buffer, sizeof buffer)) > 0 || (count < 0 && errno == EINTR)) {
      if (count > 0) payload.append(buffer, static_cast<std::size_t>(count));
      if (payload.size() > ControlSocket::kMaxRequest) break;
    }
    request += "\n" + payload;
  } else if (!options.payload.empty()) {
    request += "\n" + options.payload;
  }
  std::string reply;
  if (!controlRequest(options.controlPath(), request, reply, options.timeoutMs, error)) {
    std::fprintf(stderr, "awtrix-tc002d ctl: %s\n", error.c_str());
    return kExitFailure;
  }
  std::fwrite(reply.data(), 1, reply.size(), stdout);
  std::fputc('\n', stdout);
  return kExitOk;
}

bool prepareProcess(const DaemonOptions& options, posix::UniqueFd& lock, bool& properties) {
  ::umask(077);
  properties = PropertyWorkspace::adopt();
  sanitizeDescriptors();
  if (!posix::ensurePrivateDirectory(options.runDir)) {
    std::fprintf(stderr, "awtrix-tc002d: cannot use %s: %s\n", options.runDir.c_str(), std::strerror(errno));
    return false;
  }
  lock.reset(::open(options.lockPath().c_str(), O_RDWR | O_CREAT | O_CLOEXEC | O_NOFOLLOW, 0600));
  if (!lock.valid() || ::flock(lock.get(), LOCK_EX | LOCK_NB) < 0) {
    std::fprintf(stderr, "awtrix-tc002d: %s: %s\n", options.lockPath().c_str(),
                 errno == EWOULDBLOCK ? "another instance is running" : std::strerror(errno));
    return false;
  }
  if (!Log::open(options.logPath()))
    std::fprintf(stderr, "awtrix-tc002d: cannot open %s: %s\n", options.logPath().c_str(), std::strerror(errno));
  return true;
}

int prepareData(const DaemonOptions& options) {
  const std::vector<std::string> missing = options.missingComponents();
  if (!missing.empty()) {
    std::string list;
    for (const std::string& path : missing) list += (list.empty() ? "" : ", ") + path;
    Log::line("daemon", "broken release, not starting: %s missing or not usable", list.c_str());
    return kExitStart;
  }
  for (const std::string& directory : {options.data + "/app", options.data + "/state"}) {
    if (!posix::makeDirectories(directory, 0700)) {
      Log::line("daemon", "cannot create %s: %s", directory.c_str(), std::strerror(errno));
      return kExitUsage;
    }
  }
  if (!posix::ensurePrivateDirectory(options.data + "/state"))
    Log::line("update", "cannot make %s/state private: %s", options.data.c_str(), std::strerror(errno));

  return kExitOk;
}

UpdateServiceOptions recoverUpdate(const InstallPaths& installPaths, const RunningRelease& running) {
  UpdateServiceOptions updateOptions;
  {
    std::string writerResult, candidate, note;
    if (posix::readText(installPaths.updateResult(), writerResult, 512)) {
      while (!writerResult.empty() && (writerResult.back() == '\n' || writerResult.back() == '\r'))
        writerResult.pop_back();
      Log::line("update", "the flash helper recorded: %s", writerResult.c_str());
    }
    UpdateRecord record(installPaths.stateDir(), running.counter);
    const BootAction action = record.reconcile(running, writerResult, candidate, note);
    if (!note.empty()) Log::line("update", "%s", note.c_str());
    ::unlink(installPaths.updateResult().c_str());
    updateOptions.confirmPending = action == BootAction::Confirm;
    updateOptions.candidate = candidate;
  }
  return updateOptions;
}

RuntimeOptions makeRuntimeOptions(const DaemonOptions& options, const std::string& startReason,
                                  bool webUpdate, bool confirmPending) {
  RuntimeOptions runtimeOptions;
  runtimeOptions.executable = options.runtimePath();
  runtimeOptions.dataDir = options.data + "/app";
  runtimeOptions.webui = options.webIndexPath();
  if (regularFile(options.caFilePath())) runtimeOptions.caFile = options.caFilePath();
  else Log::line("daemon", "no CA bundle at %s; the runtime starts without --ca-file", options.caFilePath().c_str());
  if (webUpdate) {
    runtimeOptions.updateState = options.data + TC002_UPDATE_STATE;
    runtimeOptions.releaseRoot = options.root;
    runtimeOptions.updateDir = options.updateDir;
    Log::line("update", "web update enabled");
  } else {
    Log::line("update", "web update off: it needs the release slot and %s", options.flashHelperPath().c_str());
  }
  runtimeOptions.home = options.data + "/app";
  runtimeOptions.firstStartReason = startReason;
  Log::line("daemon", "the first runtime starts as %s", startReason.c_str());
  runtimeOptions.uidPath = options.deviceIdPath();
  if (!confirmPending) runtimeOptions.bootAttemptsPaths = options.bootAttemptsPaths();
  runtimeOptions.httpPort = options.httpPort;
  runtimeOptions.pcm = options.pcmBackend();
  const IntroStarts wanted = options.introStarts();
  const IntroStarts introStarts = claimPowerOnIntro(wanted, options.introShownPath());
  if (wanted == IntroStarts::First && introStarts == IntroStarts::None)
    Log::line("daemon", "boot intro already shown since power-on");
  runtimeOptions.introStarts = introStarts;
  struct stat sound{};
  if (::stat(options.bootSoundPath().c_str(), &sound) == 0 && S_ISREG(sound.st_mode))
    runtimeOptions.bootSound = options.bootSoundPath();
  if (regularFile(options.speechVoicePath())) runtimeOptions.speechVoice = options.speechVoicePath();
  else Log::line("daemon", "no voice at %s; the runtime starts without speech", options.speechVoicePath().c_str());
  Log::line("daemon", "boot intro %s: %s%s", bootIntroName(options.bootIntro),
            introStarts == IntroStarts::First   ? "first runtime start"
            : introStarts == IntroStarts::Every ? "every runtime start"
                                                : "off",
            introStarts != IntroStarts::None && runtimeOptions.bootSound.empty() ? ", no boot sound" : "");
  if (const char* timezone = std::getenv("TZ")) runtimeOptions.timezone = timezone;
  return runtimeOptions;
}

void connectMicrophone(RuntimeLinks& links, McuService& mcu, EventLoop& loop, UpdateService& update) {
  links.microphonePcm = [&](int id, RuntimeLinks::PcmReply reply) {
    const bool accepted = !loop.stopping() && !update.handOffPending() &&
        mcu.requestPcm(awtrix::tc002::kMicrophonePcmHalves, [id, reply](const mcu::PcmCapture& capture) {
          reply({id, capture.succeeded() ? capture.samples() : std::vector<int16_t>{}, capture.error()});
        }, posix::monotonicMs());
    if (!accepted) reply({id, {}, "microphone unavailable or busy"});
  };
  links.microphoneStreamAvailable = [&] { return mcu.streamAvailable(); };
  links.microphoneStreamStart = [&](int epoch, RuntimeLinks::StreamReply reply) {
    return !loop.stopping() && !update.handOffPending() && mcu.startStream(epoch, std::move(reply), posix::monotonicMs());
  };
  links.microphoneStreamControl = [&](int epoch, bool stop) {
    mcu.controlStream(epoch, stop, posix::monotonicMs());
  };
}

int finishDaemon(const DaemonOptions& options, UpdateService& update, bool rebootRequested,
                 bool started, const std::string& stopReason) {
  if (update.handOffPending()) {
    ExecPlan plan;
    std::string error;
    if (!update.prepareExec(plan, error)) {
      Log::line("update", "cannot record the update as boot-pending: %s; rebooting", error.c_str());
      update.execFailed(error);
      return rebootNow(options);
    }
    std::vector<char*> argv;
    for (const auto& argument : plan.arguments) argv.push_back(const_cast<char*>(argument.c_str()));
    argv.push_back(nullptr);
    Log::line("update", "services stopped; exec %s write-slot %s", plan.executable.c_str(), plan.arguments[2].c_str());
    ::execv(plan.executable.c_str(), argv.data());
    const std::string reason = std::string("cannot start the flash helper: ") + std::strerror(errno);
    Log::line("update", "%s; rebooting", reason.c_str());
    update.execFailed(reason);
    return rebootNow(options);
  }
  if (rebootRequested) {
    Log::line("daemon", "services stopped; syncing and rebooting");
    return rebootNow(options);
  }
  Log::line("daemon", "exit (%s)", stopReason.c_str());
  return started ? kExitOk : kExitStart;
}

McuOptions makeMcuOptions(const DaemonOptions& options) {
  McuOptions mcuOptions;
  mcuOptions.path = options.uart;
  mcuOptions.firmwareDirectory = options.root + "/share/mcu";
  mcuOptions.firmwareJournal = options.data + "/state/mcu-update.json";
  mcuOptions.firmwareCache = options.data + "/state/mcu";
  mcuOptions.firmwareHelper = options.root + "/bin/awtrix-linux";
  mcuOptions.firmwareHelperArguments = {"--prepare-mcu"};
  mcuOptions.firmwareCaFile = options.root + "/share/ca-certificates.crt";
  return mcuOptions;
}

WifiOptions makeWifiOptions(const DaemonOptions& options, bool keepAdbTcp) {
  const std::string wifiRun = options.runDir + "/wifi";
  posix::ensurePrivateDirectory(wifiRun);
  WifiOptions wifiOptions;
  wifiOptions.stateDir = options.data + "/network";
  wifiOptions.runDir = wifiRun;
  wifiOptions.keepAdbTcp = keepAdbTcp;
  wifiOptions.supplicantPath = options.supplicantPath();
  wifiOptions.moduleDirectory = options.moduleDirectory();
  return wifiOptions;
}

IpOptions makeIpOptions(const DaemonOptions& options) {
  const std::string ipRun = options.runDir + "/ip";
  posix::ensurePrivateDirectory(ipRun);
  IpOptions ipOptions;
  ipOptions.runDir = ipRun;
  ipOptions.udhcpc = options.udhcpcPath();
  ipOptions.dhcpCallback = options.dhcpCallbackPath();
  ipOptions.staticFile = options.staticAddressPath();
  ipOptions.httpPort = options.httpPort;
  return ipOptions;
}

AutostartOptions makeAutostartOptions(const DaemonOptions& options) {
  AutostartOptions autostartOptions;
  autostartOptions.path = options.autostartPath();
  autostartOptions.environment = {"PATH=/bin:/sbin:/usr/bin:/usr/sbin", "HOME=/data"};
  if (const char* timezone = std::getenv("TZ")) autostartOptions.environment.push_back(std::string("TZ=") + timezone);
  return autostartOptions;
}

int64_t logStartup(const DaemonOptions& options, bool properties) {
  const int64_t startedAt = posix::monotonicMs();
  Log::line("daemon", "awtrix-tc002d %s pid %d root %s data %s http %d wall %s", AWTRIX_NG_VERSION,
            static_cast<int>(::getpid()), options.root.c_str(), options.data.c_str(), options.httpPort,
            posix::wallClock().c_str());
  if (properties) Log::line("daemon", "property workspace fd %d kept for setprop/getprop", PropertyWorkspace::fd());
  else Log::line("daemon", "no Android property workspace inherited; setprop/getprop will not work");
  const std::string panicFailure = armPanicReboot(options.sysRoot);
  if (panicFailure.empty()) Log::line("daemon", "kernel: panic=5, panic_on_oops=1 (an oops reboots instead of hanging)");
  else Log::line("daemon", "kernel: cannot arm the panic reboot (%s)", panicFailure.c_str());
  return startedAt;
}

int runDaemon(const DaemonOptions& options) {
  posix::UniqueFd lock;
  bool properties = false;
  if (!prepareProcess(options, lock, properties)) return kExitUsage;
  ForwardedLog forwardedLog;
  const std::string startReason = firstStartReason(options);
  ::prctl(PR_SET_CHILD_SUBREAPER, 1, 0, 0, 0);
  const std::string pid = std::to_string(::getpid()) + "\n";
  if (::ftruncate(lock.get(), 0) == 0) (void)!::pwrite(lock.get(), pid.data(), pid.size(), 0);

  EventLoop loop;
  const int64_t startedAt = logStartup(options, properties);

  const int prepared = prepareData(options);
  if (prepared != kExitOk) return prepared;

  InstallPaths installPaths;
  installPaths.data = options.data;
  installPaths.updateDir = options.updateDir;
  const RunningRelease running = readRunningRelease(options.root);
  UpdateServiceOptions updateOptions = recoverUpdate(installPaths, running);
  std::string slotError;
  const uint64_t capacity = releaseSlotCapacity(options.sysRoot, slotError);
  if (capacity) Log::line("update", "release slot takes images up to %llu bytes", static_cast<unsigned long long>(capacity));
  else Log::line("update", "no release slot: %s", slotError.c_str());
  const bool keepAdbTcp = developerFlagPresent(options.keepAdbTcpFlagPath());
  LogReminder adbWarning("adb-tcp",
                         "WARNING: root ADB stays reachable over the network (developer flag " +
                             options.keepAdbTcpFlagPath() + "); remove it for normal operation",
                         kAdbWarningIntervalMs);

  DeviceState state;
  bool rebootRequested = false;

  LeaseOptions leaseOptions;
  leaseOptions.root = options.sysRoot;
  HardwareLease lease(leaseOptions, nativePanelBackend(options.sysRoot), nativeInputBackend(options.sysRoot));
  McuOptions mcuOptions = makeMcuOptions(options);
  McuService mcu(state, mcuOptions);
  ControlOptions controlOptions;
  controlOptions.path = options.controlPath();
  controlOptions.peerUid = 0;
  ControlSocket control(controlOptions);
  StockWatchOptions stockOptions;
  stockOptions.procRoot = options.sysRoot + "/proc";
  StockWatch stock(stockOptions);

  DaemonInfo info;
  info.version = AWTRIX_NG_VERSION;
  info.keepAdbTcp = keepAdbTcp;
  info.startedAtMs = startedAt;

  RuntimeLinks links;
  WifiOptions wifiOptions = makeWifiOptions(options, keepAdbTcp);
  WifiService wifi(state, wifiOptions, control);
  links.wifi = &wifi;
  IpOptions ipOptions = makeIpOptions(options);
  IpService ip(state, ipOptions);
  links.time = &ip;
  links.hostname = &ip;
  links.address = &ip;
  links.reboot = [&] {
    rebootRequested = true;
    loop.requestStop("reboot requested by the runtime");
  };

  RuntimeChild* runtimeChild = nullptr;
  const bool webUpdate = capacity > 0 && regularFile(options.flashHelperPath()) &&
                         ::access(options.flashHelperPath().c_str(), X_OK) == 0;
  updateOptions.paths = installPaths;
  updateOptions.root = options.root;
  updateOptions.capacity = webUpdate ? capacity : 0;
  updateOptions.running = running;
  if (updateOptions.confirmPending) updateOptions.bootAttemptsPaths = options.bootAttemptsPaths();
  UpdateLinks updateLinks;
  updateLinks.runtimeHealthy = [&] { return runtimeChild && runtimeChild->healthy(); };
  updateLinks.mcuFirmwareIdle = [&] { return !mcu.firmwareBusy(); };
  updateLinks.statusChanged = [&] {
    if (runtimeChild) runtimeChild->sendHello();
  };
  updateLinks.stop = [&](const std::string& reason) { loop.requestStop(reason); };
  UpdateService update(updateOptions, updateLinks);
  links.updateReady = [&](const awtrix::tc002::UpdateReady& ready) { update.handOff(ready); };
  links.hello = [&] { return update.helloDatagram(); };

  RuntimeOptions runtimeOptions = makeRuntimeOptions(options, startReason, webUpdate, updateOptions.confirmPending);
  BtService bluetooth(BtOptions{}, linuxBtSystem(), [&](const awtrix::tc002::BluetoothStatus& status) {
    if (runtimeChild) runtimeChild->bluetoothStatus(status);
  });
  links.bluetooth = [&](bool on) { bluetooth.request(on); };
  connectMicrophone(links, mcu, loop, update);
  runtimeOptions.bluetooth = true;
  RuntimeChild runtime(state, lease, runtimeOptions, links);
  runtimeChild = &runtime;
  mcu.allowFirmwareUpdate([&] {
    return !loop.stopping() && runtime.healthy() && !update.confirming() && !update.handOffPending();
  });
  const ForwardedLog::Attachment forwarding = forwardedLog.attach(runtime);

  AutostartOptions autostartOptions = makeAutostartOptions(options);
  AutostartService autostart(autostartOptions);

  control.add("status", [&](std::string_view) {
    return daemonStatus(info, state, &lease, &mcu, &runtime, posix::monotonicMs(), &update, &autostart);
  });
  control.add("bluetooth", [&](std::string_view) {
    std::string out;
    awtrix::api::JsonWriter json(out);
    json.beginObject();
    bluetooth.appendStatus(json);
    json.endObject();
    return out;
  });
  control.add("restart-runtime", [&](std::string_view) {
    Log::line("control", "runtime restart requested");
    runtime.restart(posix::monotonicMs());
    return okReply();
  });
  control.add("restart-autostart", [&](std::string_view) {
    autostart.restart(posix::monotonicMs());
    return okReply();
  });
  control.add("stop", [&](std::string_view) {
    loop.requestStop("stop requested over the control socket");
    return okReply();
  });

  StateLog stateLog(state);
  state.onPower([&] { stateLog.power(); });
  state.onNetwork([&] { stateLog.network(posix::monotonicMs()); });
  state.onTime([&] { stateLog.time(); });

  if (keepAdbTcp) loop.add(adbWarning, 0);
  loop.add(stock, 2000);
  loop.add(lease, 5000);
  loop.add(mcu, mcu::Upgrade::kLimitMs + 5000);
  loop.add(wifi, 10000);
  loop.add(bluetooth, 2000);
  loop.add(ip, 10000);
  loop.add(control, 1000);
  loop.add(runtime, 15000);
  loop.add(update, 1000);
  loop.add(autostart, 5000);

  const bool started = loop.start();
  loop.run();

  return finishDaemon(options, update, rebootRequested, started, loop.stopReason());
}

}

int main(int argc, char** argv) {
  if (argc > 1 && !std::strcmp(argv[1], "ctl")) return runCtl(argc - 1, argv + 1);
  if (argc == 2 && !std::strcmp(argv[1], "--version")) {
    std::printf("awtrix-tc002d %s\n", AWTRIX_NG_VERSION);
    return kExitOk;
  }
  if (argc == 2 && !std::strcmp(argv[1], "--help")) {
    std::fputs(usageText(), stdout);
    return kExitOk;
  }
  DaemonOptions options;
  std::string error;
  if (!parseDaemonOptions(argc, argv, options, error)) {
    std::fprintf(stderr, "awtrix-tc002d: %s\n%s", error.c_str(), usageText());
    return kExitUsage;
  }
  return runDaemon(options);
}
