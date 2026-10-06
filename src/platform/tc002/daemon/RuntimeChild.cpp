#include "platform/tc002/daemon/RuntimeChild.h"
#include "platform/tc002/contract/tc002_layout.h"

#include <fcntl.h>
#include <poll.h>
#include <sys/socket.h>
#include <sys/wait.h>
#include <unistd.h>

#include <algorithm>
#include <cerrno>
#include <csignal>
#include <cstdio>
#include <cstdlib>
#include <cstring>

#include "platform/tc002/contract/RuntimeContract.h"
#include "platform/tc002/contract/SupervisorProtocol.h"
#include "platform/tc002/daemon/Log.h"
#include "platform/tc002/daemon/Process.h"

namespace awtrix {
namespace tc002d {
namespace {

using tc002::kAudioFd;
using tc002::kKeysFd;
using tc002::kKnobFd;
using tc002::kSupervisorFd;

const char* helperExitMeaning(int status) {
  if (!WIFEXITED(status)) return "killed";
  switch (WEXITSTATUS(status)) {
    case 0: return "clean";
    case 1: return "failure";
    case 2: return "usage";
    case 3: return "preflight refused";
    case 4: return "device unavailable";
    case 5: return "start";
    case 124: return "call watchdog";
    case 127: return "exec failed";
  }
  return "unknown";
}


}

RuntimeChild::RuntimeChild(DeviceState& state, ChildHardware& hardware, RuntimeOptions options, RuntimeLinks links)
    : device_(state),
      hardware_(hardware),
      options_(std::move(options)),
      links_(std::move(links)),
      speaker_(SpeakerOptions{options_.pcm, options_.pcmOwner, options_.moduleLoadTimeoutMs, options_.loadModule}),
      deviceId_(options_.uidPath),
      microphone_(links_, {channel_, hello_, pid_, spawns_, sendErrors_, alive_}) {
  if (options_.backoffMs.empty()) options_.backoffMs.push_back(1000);
  startReason_ = options_.firstStartReason;
  if (!options_.pcm.helper.empty()) helperState_ = "idle";
  device_.onPower([this] { forward(tc002::encodePower(device_.power())); });
  device_.onNetwork([this] { networkChanged(); });
  device_.onTime([this] { forward(tc002::encodeTime(device_.time())); });
}

RuntimeChild::~RuntimeChild() {
  microphone_.revokeStream();
  const int64_t giveUpAt = posix::monotonicMs() + kReapGraceMs;
  if (pid_ > 0) {
    signalChild(SIGKILL);
    reapUntil(pid_, killed_ ? 0 : giveUpAt);
  }
  if (helperPid_ > 0) {
    ::kill(helperPid_, SIGKILL);
    reapUntil(helperPid_, helperStuck_ ? 0 : giveUpAt);
  }
}

const char* RuntimeChild::stateName(State state) {
  switch (state) {
    case State::Waiting: return "waiting";
    case State::Starting: return "starting";
    case State::Running: return "running";
    case State::Terminating: return "terminating";
    case State::Stopped: return "stopped";
  }
  return "stopped";
}

bool RuntimeChild::introOnNextStart() const {
  return options_.introStarts == IntroStarts::Every || (options_.introStarts == IntroStarts::First && spawns_ == 0);
}

std::vector<std::string> RuntimeChild::arguments(bool audio) const {
  std::vector<std::string> args{options_.executable, "--board", "tc002", tc002::kInputFdsFlag, tc002::inputFdsValue(),
                                tc002::kSupervisorFdFlag, std::to_string(kSupervisorFd), "--data", options_.dataDir,
                                "--webui", options_.webui, "--lan", "--port", std::to_string(options_.httpPort),
                                tc002::kStartReasonFlag, startReason_};
  if (!options_.uidPath.empty() && !deviceId_.value().empty()) {
    args.push_back(tc002::kUidFlag);
    args.push_back(deviceId_.value());
  }
  if (audio) {
    args.push_back(tc002::kAudioFdFlag);
    args.push_back(std::to_string(kAudioFd));
    if (!options_.speechVoice.empty()) {
      args.push_back("--speech-voice");
      args.push_back(options_.speechVoice);
    }
  }
  if (options_.bluetooth) args.push_back(tc002::kBluetoothFlag);
  if (introOnNextStart()) {
    args.push_back("--boot-intro");
    if (!options_.bootSound.empty()) {
      args.push_back("--boot-sound");
      args.push_back(options_.bootSound);
    }
  }
  const std::pair<const char*, const std::string*> passed[] = {{"--ca-file", &options_.caFile},
                                                               {"--update-state", &options_.updateState},
                                                               {"--release-root", &options_.releaseRoot},
                                                               {tc002::kUpdateDirFlag, &options_.updateDir}};
  for (const auto& [flag, value] : passed) {
    if (value->empty()) continue;
    args.push_back(flag);
    args.push_back(*value);
  }
  return args;
}

int RuntimeChild::spawnHelper(int64_t nowMs) {
  (void)nowMs;
  const std::string& helper = speaker_.helper();
  if (helper.empty()) {
    if (speaker_.kind() == SpeakerBackend::Kind::Off) helperState_ = "off";
    return -1;
  }
  if (helperPid_ > 0) {
    Log::line("audio", "previous speaker helper pid %d still present; starting without audio",
              static_cast<int>(helperPid_));
    return -1;
  }
  if (::access(helper.c_str(), X_OK) < 0) {
    if (helperState_ != "missing")
      Log::line("audio", "speaker helper %s unavailable (%s); starting without audio", helper.c_str(),
                std::strerror(errno));
    helperState_ = "missing";
    return -1;
  }
  int pair[2];
  if (::socketpair(AF_UNIX, SOCK_SEQPACKET | SOCK_CLOEXEC, 0, pair) < 0) {
    Log::line("audio", "cannot create the speaker channel: %s", std::strerror(errno));
    helperState_ = "failed";
    return -1;
  }
  posix::UniqueFd runtimeEnd(pair[0]), helperEnd(pair[1]);
  ProcessSpec spec;
  spec.path = helper;
  spec.argv = {helper, "--socket-fd", std::to_string(kAudioFd)};
  spec.descriptors = {{helperEnd.get(), kAudioFd}};
  spec.group = ProcessGroup::Session;
  std::string error;
  const pid_t pid = spawnProcess(spec, error);
  if (pid < 0) {
    Log::line("audio", "cannot start the speaker helper: %s", error.c_str());
    helperState_ = "failed";
    return -1;
  }
  helperPid_ = pid;
  helperStopAt_ = -1;
  helperTermSent_ = false;
  ++helperStarts_;
  helperState_ = "running";
  Log::line("audio", "speaker helper pid %d started", static_cast<int>(pid));
  return runtimeEnd.release();
}

bool RuntimeChild::start(int64_t nowMs) {
  stopRequested_ = false;
  state_ = State::Waiting;
  spawnAt_ = nowMs;
  uidWaitUntil_ = nowMs + options_.uidWaitMs;
  speaker_.begin(nowMs);
  if (speaker_.kind() == SpeakerBackend::Kind::Off) helperState_ = "off";
  return true;
}

void RuntimeChild::spawn(int64_t nowMs) {
  int channel[2], output[2];
  if (::socketpair(AF_UNIX, SOCK_SEQPACKET | SOCK_CLOEXEC | SOCK_NONBLOCK, 0, channel) < 0) {
    Log::line("runtime", "cannot create supervisor channel: %s", std::strerror(errno));
    spawnAt_ = nowMs + options_.backoffMs.front();
    return;
  }
  posix::UniqueFd channelOurs(channel[0]), channelChild(channel[1]);
  if (::pipe2(output, O_CLOEXEC) < 0) {
    Log::line("runtime", "cannot create output pipe: %s", std::strerror(errno));
    spawnAt_ = nowMs + options_.backoffMs.front();
    return;
  }
  posix::UniqueFd outputRead(output[0]), outputWrite(output[1]);
  ::fcntl(outputRead.get(), F_SETFL, O_NONBLOCK);
  posix::UniqueFd audio(spawnHelper(nowMs));

  const bool intro = introOnNextStart();
  ProcessSpec spec;
  spec.path = options_.executable;
  spec.argv = arguments(audio.valid());
  spec.environment = {"PATH=/bin:/sbin:/usr/bin:/usr/sbin", "HOME=" + options_.home};
  if (!options_.timezone.empty()) spec.environment.push_back("TZ=" + options_.timezone);
  spec.descriptors = {{outputWrite.get(), STDOUT_FILENO},
                      {outputWrite.get(), STDERR_FILENO},
                      {hardware_.keysFd(), kKeysFd},
                      {hardware_.knobFd(), kKnobFd},
                      {channelChild.get(), kSupervisorFd}};
  if (audio.valid()) spec.descriptors.push_back({audio.get(), kAudioFd});
  spec.group = ProcessGroup::Session;
  std::string error;
  const pid_t pid = spawnProcess(spec, error);
  if (pid < 0) {
    Log::line("runtime", "cannot start the runtime: %s", error.c_str());
    spawnAt_ = nowMs + options_.backoffMs.front();
    return;
  }
  pid_ = pid;
  channel_ = std::move(channelOurs);
  output_ = std::move(outputRead);
  logs_.newOutput();
  hello_ = resyncPending_ = healthy_ = killed_ = termSent_ = false;
  state_ = State::Starting;
  spawnedAt_ = nowMs;
  readyDeadline_ = nowMs + options_.readyTimeoutMs;
  spawnAt_ = -1;
  ++spawns_;
  introGiven_ = intro;
  startedUid_ = deviceId_.value();
  Log::line("runtime", "started pid %d (start %u)%s", static_cast<int>(pid), spawns_,
            intro ? (options_.bootSound.empty() ? " with the boot intro" : " with the boot intro and sound") : "");
}

// The first start waits for the Wi-Fi MAC while no device id is kept, so that the first runtime
// already has the id its API and MQTT name it by; networkChanged() ends the wait.
bool RuntimeChild::awaitUid(int64_t nowMs) {
  if (options_.uidPath.empty() || spawns_ > 0 || !deviceId_.value().empty()) return false;
  if (nowMs < uidWaitUntil_) {
    if (!uidWaiting_)
      Log::line("runtime", "no device id kept yet; waiting up to %lld ms for the Wi-Fi MAC",
                static_cast<long long>(uidWaitUntil_ - nowMs));
    uidWaiting_ = true;
    spawnAt_ = uidWaitUntil_;
    return true;
  }
  if (uidWaiting_) Log::line("runtime", "starting without a device id: the Wi-Fi MAC is not known yet");
  uidWaiting_ = false;
  return false;
}

void RuntimeChild::networkChanged() {
  forward(tc002::encodeNetwork(device_.network()));
  if (options_.uidPath.empty() || !deviceId_.observe(device_.network().mac)) return;
  if (uidWaiting_) {
    uidWaiting_ = false;
    spawnAt_ = posix::monotonicMs();
    return;
  }
  if ((state_ != State::Starting && state_ != State::Running) || startedUid_ == deviceId_.value() || uidRestarted_)
    return;
  uidRestarted_ = true;
  Log::line("runtime", "pid %d runs without device id %s; restarting it", static_cast<int>(pid_),
            deviceId_.value().c_str());
  restart(posix::monotonicMs());
}

void RuntimeChild::signalChild(int signal) {
  if (pid_ <= 0) return;
  if (::kill(-pid_, signal) < 0) ::kill(pid_, signal);
}

void RuntimeChild::terminate(int64_t nowMs, const char* reason, int64_t graceMs) {
  if (state_ != State::Starting && state_ != State::Running) return;
  microphone_.revokeStream();
  state_ = State::Terminating;
  logs_.disconnect();
  killed_ = termSent_ = false;
  killAt_ = -1;
  if (graceMs > 0) {
    Log::line("runtime", "waiting up to %lld ms for pid %d to exit: %s", static_cast<long long>(graceMs),
              static_cast<int>(pid_), reason);
    termAt_ = nowMs + graceMs;
    return;
  }
  Log::line("runtime", "stopping pid %d: %s", static_cast<int>(pid_), reason);
  sendTerm(nowMs);
}

void RuntimeChild::sendTerm(int64_t nowMs) {
  signalChild(SIGTERM);
  termSent_ = true;
  termAt_ = -1;
  killAt_ = nowMs + options_.stopGraceMs;
}

void RuntimeChild::requestStop(int64_t nowMs) {
  microphone_.revokeStream();
  stopRequested_ = true;
  manualRestart_ = false;
  if (state_ == State::Running) clearBootAttempts("daemon stopping with the runtime ready");
  if (state_ == State::Starting || state_ == State::Running)
    terminate(nowMs, rebootPending_ ? "reboot requested" : "daemon stopping",
              rebootPending_ ? options_.rebootExitGraceMs : 0);
  else if (state_ != State::Terminating) state_ = State::Stopped;
}

void RuntimeChild::restart(int64_t nowMs) {
  if (stopRequested_) return;
  failures_ = 0;
  if (state_ == State::Waiting) {
    spawnAt_ = nowMs;
    return;
  }
  manualRestart_ = true;
  terminate(nowMs, "restart requested");
}

void RuntimeChild::pollInterest(std::vector<PollInterest>& out) const {
  if (channel_.valid()) out.push_back({channel_.get(),
      static_cast<short>(POLLIN | (resyncPending_ || microphone_.queued() ? POLLOUT : 0))});
  if (output_.valid()) out.push_back({output_.get(), POLLIN});
}

void RuntimeChild::onReady(int fd, short revents, int64_t nowMs) {
  if (fd < 0) return;
  if (fd == channel_.get()) {
    if (revents & (POLLIN | POLLHUP | POLLERR)) readChannel(nowMs, false);
    if (channel_.valid() && (revents & POLLOUT)) {
      microphone_.flushStream();
      microphone_.flushPcm();
      if (!microphone_.pcmQueued() && resyncPending_) sendSnapshot();
    }
  } else if (fd == output_.get()) {
    logs_.readOutput(output_, false);
  }
}

// The TC002 runtime drives the 52x16 panel and reads the buttons; anything else was started wrong.
void RuntimeChild::markReady(const tc002::RuntimeReady& ready, int64_t nowMs) {
  if (state_ != State::Starting) {
    if (badMessages_++ % 100 < 5) Log::line("runtime", "ignored a second readiness report");
    return;
  }
  if (!hello_ || ready.board != "tc002" || ready.width != TC002_PANEL_WIDTH || ready.height != TC002_PANEL_HEIGHT || !ready.input) {
    Log::line("runtime", "invalid readiness report (%s, board %s %dx%d %s input)",
              hello_ ? "after hello" : "before hello", ready.board.c_str(), ready.width, ready.height,
              ready.input ? "with" : "without");
    readinessFailed_ = true;
    terminate(nowMs, "invalid readiness report");
    return;
  }
  state_ = State::Running;
  readyAt_ = nowMs;
  healthyAt_ = nowMs + options_.healthyMs;
  Log::line("runtime", "pid %d ready after %lld ms", static_cast<int>(pid_),
            static_cast<long long>(nowMs - spawnedAt_));
}

void RuntimeChild::readChannel(int64_t nowMs, bool final) {
  char buffer[tc002::kMaxSupervisorMessage + 1];
  for (unsigned round = 0; (final || round < 16) && channel_.valid(); ++round) {
    const ssize_t count = ::recv(channel_.get(), buffer, sizeof buffer, MSG_DONTWAIT);
    if (count < 0 && errno == EINTR) continue;
    if (count < 0 && (errno == EAGAIN || errno == EWOULDBLOCK)) return;
    if (count <= 0) {
      microphone_.revokeStream();
      channel_.reset();
      hello_ = resyncPending_ = false;
      logs_.disconnect();
      microphone_.discardPcmReply();
      if (!final && state_ == State::Starting) terminate(nowMs, "supervisor channel closed before the ready report");
      return;
    }
    dispatch(std::string(buffer, static_cast<std::size_t>(count)), nowMs);
  }
}

void RuntimeChild::dispatch(const std::string& datagram, int64_t nowMs) {
  tc002::SupervisorMessage message;
  if (!tc002::decodeSupervisorMessage(datagram, message)) {
    if (badMessages_++ % 100 < 5) Log::line("runtime", "ignored malformed message (%zu bytes)", datagram.size());
    return;
  }
  using tc002::MessageType;
  switch (message.type) {
    case MessageType::Hello:
      hello_ = true;
      runtimeVersion_ = message.version;
      Log::line("runtime", "hello from runtime %s", message.version.c_str());
      sendSnapshot();
      logs_.hello(channel_.get());
      break;
    case MessageType::Ready:
      markReady(message.ready, nowMs);
      break;
    case MessageType::Wifi:
      if (!links_.wifi) {
        Log::line("runtime", "Wi-Fi credentials for \"%s\" dropped: no Wi-Fi service", message.wifi.ssid.c_str());
        break;
      }
      Log::line("runtime", "Wi-Fi credentials for \"%s\" %s", message.wifi.ssid.c_str(),
                links_.wifi->setCredentials(message.wifi) ? "accepted" : "refused");
      break;
    case MessageType::Ntp:
      if (ntpKnown_ && message.ntpServer == ntpServer_) break;
      ntpKnown_ = true;
      ntpServer_ = message.ntpServer;
      Log::line("runtime", "NTP server \"%s\"", message.ntpServer.c_str());
      if (links_.time) links_.time->setServer(message.ntpServer);
      break;
    case MessageType::Hostname:
      if (hostnameKnown_ && message.hostname == hostname_) break;
      hostnameKnown_ = true;
      hostname_ = message.hostname;
      Log::line("runtime", "hostname \"%s\"", message.hostname.c_str());
      if (links_.hostname) links_.hostname->setHostname(message.hostname);
      break;
    case MessageType::Address:
      if (links_.address) links_.address->setStaticAddress(message.address);
      break;
    case MessageType::Reboot:
      Log::line("runtime", "runtime requested a reboot");
      if (state_ == State::Running) clearBootAttempts("reboot requested by the ready runtime");
      rebootPending_ = true;
      if (links_.reboot) links_.reboot();
      break;
    case MessageType::FactoryReset:
      Log::line("runtime", "runtime requested a factory reset");
      if (links_.wifi) links_.wifi->eraseCredentials();
      else Log::line("runtime", "no Wi-Fi service; no stored credentials to erase");
      if (links_.address) links_.address->setStaticAddress(tc002::StaticAddress());
      break;
    case MessageType::WifiScan:
      requestScan();
      break;
    case MessageType::MicrophonePcmRequest:
      microphone_.requestPcm(message.microphonePcm.id, nowMs);
      break;
    case MessageType::MicrophoneStreamControl:
      if (state_ == State::Running) microphone_.requestStream(message.streamControl, nowMs);
      break;
    case MessageType::Bluetooth:
      if (links_.bluetooth) links_.bluetooth(message.bluetooth.on);
      else bluetoothStatus(tc002::BluetoothStatus{false, "this supervisor has no Bluetooth"});
      break;
    case MessageType::UpdateReady:
      Log::line("runtime", "update %s (counter %llu) handed off", message.updateReady.release.c_str(),
                static_cast<unsigned long long>(message.updateReady.counter));
      if (links_.updateReady) links_.updateReady(message.updateReady);
      else Log::line("runtime", "this daemon cannot install updates");
      break;
    case MessageType::Power:
    case MessageType::Network:
    case MessageType::Time:
    case MessageType::MicrophonePcm:
    case MessageType::MicrophoneStreamEvent:
    case MessageType::WifiScanResult:
    case MessageType::Log:
    case MessageType::Invalid:
      if (badMessages_++ % 100 < 5) Log::line("runtime", "ignored supervisor-only message from runtime");
      break;
  }
}

void RuntimeChild::bluetoothStatus(const tc002::BluetoothStatus& status) {
  bluetoothStatus_ = status;
  bluetoothKnown_ = true;
  forward(tc002::encodeBluetooth(status));
}

void RuntimeChild::forward(const std::string& message) {
  if (!channel_.valid() || !hello_ || resyncPending_ || message.empty()) return;
  if (::send(channel_.get(), message.data(), message.size(), MSG_DONTWAIT | MSG_NOSIGNAL) >= 0) return;
  if (errno == EAGAIN || errno == EWOULDBLOCK || errno == ENOBUFS) {
    resyncPending_ = true;
    return;
  }
  if (sendErrors_++ < 5) Log::line("runtime", "send to runtime failed: %s", std::strerror(errno));
}

void RuntimeChild::requestScan() {
  if (!links_.wifi) {
    if (scanNotices_++ % 100 == 0) Log::line("runtime", "no Wi-Fi service; answering a scan with an empty list");
    forward(tc002::encodeWifiScanResult({}));
    return;
  }
  const std::weak_ptr<char> alive = alive_;
  const unsigned generation = spawns_;
  links_.wifi->scan([this, alive, generation](const std::vector<tc002::WifiNetwork>& networks) {
    if (alive.expired() || generation != spawns_ || pid_ <= 0) return;
    forward(tc002::encodeWifiScanResult(networks));
  });
}

void RuntimeChild::sendHello() {
  if (links_.hello) forward(links_.hello());
}

void RuntimeChild::sendSnapshot() {
  resyncPending_ = false;
  for (const std::string& message : {links_.hello ? links_.hello() : std::string(), tc002::encodePower(device_.power()),
                                     tc002::encodeNetwork(device_.network()), tc002::encodeTime(device_.time()),
                                     bluetoothKnown_ ? tc002::encodeBluetooth(bluetoothStatus_) : std::string()}) {
    forward(message);
    if (resyncPending_) return;
  }
}

void RuntimeChild::closeChannels(int64_t nowMs) {
  microphone_.revokeStream();
  logs_.readOutput(output_, true);
  readChannel(nowMs, true);
  channel_.reset();
  output_.reset();
  hello_ = resyncPending_ = false;
  logs_.disconnect();
  microphone_.discardPcmReply();
}

bool RuntimeChild::onChildExit(pid_t pid, int status, int64_t nowMs) {
  if (speaker_.onChildExit(pid, status, nowMs)) return true;
  if (helperPid_ > 0 && pid == helperPid_) {
    helperPid_ = -1;
    helperStopAt_ = -1;
    helperTermSent_ = helperKilled_ = helperStuck_ = false;
    helperLastExit_ = describeWait(status) + " (" + helperExitMeaning(status) + ")";
    helperState_ = WIFEXITED(status) && WEXITSTATUS(status) == 0 ? "exited" : "failed";
    Log::line("audio", "speaker helper pid %d ended: %s", static_cast<int>(pid), helperLastExit_.c_str());
    speaker_.helperEnded(status);
    return true;
  }
  if (pid_ <= 0 || pid != pid_) return false;
  lastExit_ = describeWait(status);
  startReason_ = manualRestart_                                  ? tc002::kStartSoftware
                 : readinessFailed_                              ? tc002::kStartWatchdog
                 : WIFEXITED(status) && WEXITSTATUS(status) == 0 ? tc002::kStartSoftware
                                                                 : tc002::kStartPanic;
  readinessFailed_ = false;
  closeChannels(nowMs);
  bluetoothKnown_ = false;
  if (links_.bluetooth) links_.bluetooth(false);
  Log::line("runtime", "pid %d ended (%s) after %lld ms", static_cast<int>(pid), lastExit_.c_str(),
            static_cast<long long>(nowMs - spawnedAt_));
  pid_ = -1;
  killAt_ = readyDeadline_ = healthyAt_ = -1;
  if (helperPid_ > 0) helperStopAt_ = nowMs + options_.helperExitGraceMs;
  if (stopRequested_) {
    state_ = State::Stopped;
    return true;
  }
  state_ = State::Waiting;
  if (manualRestart_) {
    manualRestart_ = false;
    spawnAt_ = nowMs;
    return true;
  }
  ++failures_;
  const std::size_t index = failures_ - 1 < options_.backoffMs.size() ? failures_ - 1 : options_.backoffMs.size() - 1;
  spawnAt_ = nowMs + options_.backoffMs[index];
  Log::line("runtime", "restarting in %lld ms (consecutive failures %u)",
            static_cast<long long>(options_.backoffMs[index]), failures_);
  return true;
}

void RuntimeChild::becameHealthy(int64_t nowMs) {
  healthy_ = true;
  failures_ = 0;
  Log::line("runtime", "healthy for %lld ms", static_cast<long long>(nowMs - readyAt_));
  clearBootAttempts("runtime healthy");
}

void RuntimeChild::clearBootAttempts(const char* reason) {
  for (const std::string& path : options_.bootAttemptsPaths) {
    if (::unlink(path.c_str()) == 0) {
      const bool synced = posix::fsyncDirectory(posix::parentDirectory(path));
      Log::line("runtime", "cleared loader boot counter %s: %s%s", path.c_str(), reason,
                synced ? "" : " (directory sync failed)");
    } else if (errno != ENOENT) {
      Log::line("runtime", "cannot clear loader boot counter %s: %s", path.c_str(), std::strerror(errno));
    }
  }
}

// Only while no runtime holds the other end of its socket: the helper should exit by itself;
// after the grace period it gets SIGTERM, then SIGKILL. One that survives even that (stuck in
// the driver) no longer blocks the runtime, which then starts without audio.
bool RuntimeChild::helperBlocksStart(int64_t nowMs) {
  if (helperPid_ <= 0 || pid_ > 0) return false;
  if (helperStuck_) return false;
  if (helperStopAt_ < 0) helperStopAt_ = nowMs + options_.helperExitGraceMs;
  if (nowMs < helperStopAt_) return true;
  if (!helperTermSent_) {
    Log::line("audio", "speaker helper pid %d still running; sending SIGTERM", static_cast<int>(helperPid_));
    ::kill(helperPid_, SIGTERM);
    helperTermSent_ = true;
    helperStopAt_ = nowMs + 2000;
    return true;
  }
  if (!helperKilled_) {
    Log::line("audio", "speaker helper pid %d ignored SIGTERM; sending SIGKILL", static_cast<int>(helperPid_));
    ::kill(helperPid_, SIGKILL);
    helperKilled_ = true;
    helperStopAt_ = nowMs + 2000;
    return true;
  }
  Log::line("audio", "speaker helper pid %d cannot be reaped; runtime starts without audio",
            static_cast<int>(helperPid_));
  helperStuck_ = true;
  helperState_ = "stuck";
  helperStopAt_ = -1;
  return false;
}

int64_t RuntimeChild::nextDeadlineMs() const {
  const int64_t own = stateDeadlineMs(), speaker = speaker_.nextDeadlineMs();
  if (own < 0) return speaker;
  return speaker < 0 || own < speaker ? own : speaker;
}

int64_t RuntimeChild::stateDeadlineMs() const {
  const bool helperPending = helperPid_ > 0 && pid_ <= 0 && !helperStuck_;
  switch (state_) {
    case State::Waiting:
      if (stopRequested_) return helperPending ? helperStopAt_ : -1;
      if (speaker_.pending()) return -1;
      return helperPending && helperStopAt_ >= 0 ? helperStopAt_ : spawnAt_;
    case State::Starting: return readyDeadline_;
    case State::Running: return healthy_ ? -1 : healthyAt_;
    case State::Terminating: return !termSent_ ? termAt_ : killed_ ? -1 : killAt_;
    case State::Stopped: return helperPending ? helperStopAt_ : -1;
  }
  return -1;
}

void RuntimeChild::onTime(int64_t nowMs) {
  speaker_.onTime(nowMs);
  switch (state_) {
    case State::Waiting:
      if (helperBlocksStart(nowMs)) return;
      if (stopRequested_ || spawnAt_ < 0 || nowMs < spawnAt_ || speaker_.pending()) return;
      if (awaitUid(nowMs)) return;
      if (!hardware_.childReady()) {
        if (prepareWaits_++ % 100 == 0) Log::line("runtime", "waiting for the hardware lease");
        spawnAt_ = nowMs + options_.prepareRetryMs;
        return;
      }
      if (!hardware_.prepareChild(nowMs)) {
        spawnAt_ = nowMs + options_.prepareRetryMs;
        return;
      }
      prepareWaits_ = 0;
      spawn(nowMs);
      return;
    case State::Starting:
      if (nowMs >= readyDeadline_) {
        readinessFailed_ = true;
        terminate(nowMs, "no readiness report in time");
      }
      return;
    case State::Running:
      if (!healthy_ && nowMs >= healthyAt_) becameHealthy(nowMs);
      return;
    case State::Terminating:
      if (!termSent_) {
        if (nowMs < termAt_) return;
        Log::line("runtime", "pid %d still running; sending SIGTERM", static_cast<int>(pid_));
        sendTerm(nowMs);
        return;
      }
      if (!killed_ && nowMs >= killAt_) {
        Log::line("runtime", "pid %d ignored SIGTERM; sending SIGKILL", static_cast<int>(pid_));
        signalChild(SIGKILL);
        killed_ = true;
      }
      return;
    case State::Stopped:
      helperBlocksStart(nowMs);
      return;
  }
}

void RuntimeChild::appendStatus(api::JsonWriter& json, int64_t nowMs) const {
  json.key("runtime").beginObject()
      .member("state", stateName(state_))
      .member("pid", static_cast<int>(pid_))
      .member("starts", spawns_)
      .member("restarts", spawns_ > 0 ? spawns_ - 1 : 0u)
      .member("consecutiveFailures", failures_)
      .member("hello", hello_)
      .member("version", runtimeVersion_)
      .member("lastExit", lastExit_)
      .member("bootIntro", introGiven_)
      .member("uid", deviceId_.value());
  if (pid_ > 0) json.member("uptimeMs", static_cast<long long>(nowMs - spawnedAt_));
  if (state_ == State::Waiting && spawnAt_ >= 0)
    json.member("nextStartInMs", static_cast<long long>(spawnAt_ > nowMs ? spawnAt_ - nowMs : 0));
  json.endObject();
  json.key("audio").beginObject()
      .member("helper", speaker_.helper())
      .member("state", helperState_)
      .member("pid", static_cast<int>(helperPid_))
      .member("starts", helperStarts_)
      .member("lastExit", helperLastExit_)
      .member("backend", speaker_.kindName())
      .member("backendNote", speaker_.note())
      .endObject();
}


}
}
