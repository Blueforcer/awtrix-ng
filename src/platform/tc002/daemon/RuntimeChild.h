#pragma once

#include <sys/types.h>

#include <cstdint>
#include <deque>
#include <functional>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

#include "core/api/JsonWriter.h"
#include "platform/posix/Files.h"
#include "platform/tc002/contract/RuntimeContract.h"
#include "platform/tc002/contract/SupervisorProtocol.h"
#include "platform/tc002/daemon/DeviceId.h"
#include "platform/tc002/daemon/RuntimeLog.h"
#include "platform/tc002/daemon/RuntimeLinks.h"
#include "platform/tc002/daemon/MicrophoneRelay.h"
#include "platform/tc002/daemon/HardwareLease.h"
#include "platform/tc002/daemon/Options.h"
#include "platform/tc002/daemon/Service.h"
#include "platform/tc002/daemon/SpeakerBackend.h"

// Runs awtrix-linux as the supervised child with the descriptors of RuntimeContract.h: the input
// devices from the lease and the SOCK_SEQPACKET supervisor channel, on which the runtime reports
// ready. Restarts on exit, crash or missing readiness with backoff; after healthyMs of readiness
// the backoff resets and the loader's boot-attempt counters are removed, as they are when a reboot
// or a stop is requested while the runtime is ready.
namespace awtrix {
namespace tc002d {

struct RuntimeOptions {
  std::string executable;
  std::string dataDir;
  std::string webui;
  std::string home;
  std::string timezone;
  // Passed when set: the CA bundle, and the web update (state file, running release and the
  // directory the runtime stages packages in).
  std::string caFile;
  std::string updateState;
  std::string releaseRoot;
  std::string updateDir;
  // The loader counts starts in /data and falls back to /tmp when /data cannot take the counter.
  std::vector<std::string> bootAttemptsPaths;
  int httpPort = 80;
  int64_t readyTimeoutMs = 30000;
  int64_t healthyMs = 60000;
  int64_t stopGraceMs = 5000;
  int64_t rebootExitGraceMs = 3000;
  // Speaker helper pcm.helper, started next to every runtime once awtrix_pcm is loaded (empty: no
  // audio; SpeakerBackend). It exits by itself once the runtime's end of its socket closes; a new
  // one is started only after the old one is reaped. pcmOwner owns the module; loadModule
  // replaces finit_module in the forked loader.
  PcmBackendPaths pcm;
  int64_t helperExitGraceMs = 3000;
  uid_t pcmOwner = 0;
  int64_t moduleLoadTimeoutMs = 10000;
  std::function<int(int fd)> loadModule;
  // Why the first runtime starts (RuntimeContract.h); a later start follows from how the runtime
  // before it ended.
  std::string firstStartReason = tc002::kStartPowerOn;
  // Where the device id is kept (DeviceId); every runtime gets it as --uid. Empty: no --uid, the
  // runtime uses an id of its own. While no id is kept, the first start waits up to uidWaitMs for
  // the Wi-Fi MAC. A runtime that got no id or another one is restarted once the id is known,
  // once per daemon.
  std::string uidPath;
  int64_t uidWaitMs = 20000;
  // The starts that show the power-on intro (--boot-intro), with bootSound playing along when set.
  IntroStarts introStarts = IntroStarts::None;
  std::string bootSound;
  // The voice file, passed as --speech-voice to every runtime that gets the speaker.
  std::string speechVoice;
  int64_t prepareRetryMs = 100;
  std::vector<int64_t> backoffMs{1000, 2000, 5000, 10000, 30000};
  // Whether the runtime may ask for the Bluetooth controller (kBluetoothFlag).
  bool bluetooth = false;
};

class RuntimeChild : public Service {
 public:
  enum class State { Waiting, Starting, Running, Terminating, Stopped };

  RuntimeChild(DeviceState& state, ChildHardware& hardware, RuntimeOptions options, RuntimeLinks links);
  ~RuntimeChild() override;

  const char* name() const override { return "runtime"; }
  bool start(int64_t nowMs) override;
  void pollInterest(std::vector<PollInterest>& out) const override;
  void onReady(int fd, short revents, int64_t nowMs) override;
  int64_t nextDeadlineMs() const override;
  void onTime(int64_t nowMs) override;
  bool onChildExit(pid_t pid, int status, int64_t nowMs) override;
  void requestStop(int64_t nowMs) override;
  bool stopped() const override { return state_ == State::Stopped && (helperPid_ <= 0 || helperStuck_); }

  // Operator restart: terminate the current child and start a new one without backoff.
  void restart(int64_t nowMs);
  void sendHello();
  // Tells the runtime whether the Bluetooth controller it asked for is attached.
  void bluetoothStatus(const tc002::BluetoothStatus& status);
  void logLine(const char* component, std::string_view text) {
    logs_.line(channel_.get(), component, text);
  }
  bool healthy() const { return state_ == State::Running && healthy_; }
  void appendStatus(api::JsonWriter& json, int64_t nowMs) const;

  State state() const { return state_; }
  pid_t pid() const { return pid_; }
  unsigned spawns() const { return spawns_; }
  unsigned consecutiveFailures() const { return failures_; }
  bool helloReceived() const { return hello_; }
  const std::string& runtimeVersion() const { return runtimeVersion_; }
  const std::string& lastExit() const { return lastExit_; }
  pid_t helperPid() const { return helperPid_; }
  unsigned helperStarts() const { return helperStarts_; }
  const std::string& helperState() const { return helperState_; }
  const std::string& helperLastExit() const { return helperLastExit_; }
  const SpeakerBackend& speaker() const { return speaker_; }
  // Whether the next start shows the power-on intro, and whether the current runtime got it.
  bool introOnNextStart() const;
  bool introGiven() const { return introGiven_; }
  std::vector<std::string> arguments(bool audio) const;
  static const char* stateName(State state);

 private:
  int64_t stateDeadlineMs() const;
  void spawn(int64_t nowMs);
  bool awaitUid(int64_t nowMs);
  void networkChanged();
  int spawnHelper(int64_t nowMs);
  bool helperBlocksStart(int64_t nowMs);
  void terminate(int64_t nowMs, const char* reason, int64_t graceMs = 0);
  void sendTerm(int64_t nowMs);
  void signalChild(int signal);
  void markReady(const tc002::RuntimeReady& ready, int64_t nowMs);
  void readChannel(int64_t nowMs, bool final);
  void dispatch(const std::string& datagram, int64_t nowMs);
  void forward(const std::string& message);
  void sendSnapshot();
  void becameHealthy(int64_t nowMs);
  void clearBootAttempts(const char* reason);
  void closeChannels(int64_t nowMs);
  void requestScan();
  DeviceState& device_;
  ChildHardware& hardware_;
  RuntimeOptions options_;
  RuntimeLinks links_;
  SpeakerBackend speaker_;
  DeviceId deviceId_;
  std::string startedUid_;
  int64_t uidWaitUntil_ = -1;
  bool uidWaiting_ = false;
  bool uidRestarted_ = false;

  State state_ = State::Stopped;
  pid_t pid_ = -1;
  posix::UniqueFd channel_, output_;
  RuntimeLog logs_;

  bool stopRequested_ = false;
  bool manualRestart_ = false;
  bool readinessFailed_ = false;
  std::string startReason_;
  bool rebootPending_ = false;
  bool termSent_ = false;
  bool hello_ = false;
  bool resyncPending_ = false;
  tc002::BluetoothStatus bluetoothStatus_;
  bool bluetoothKnown_ = false;
  bool healthy_ = false;
  bool killed_ = false;
  bool introGiven_ = false;

  int64_t spawnAt_ = -1;
  int64_t readyDeadline_ = -1;
  int64_t healthyAt_ = -1;
  int64_t termAt_ = -1;
  int64_t killAt_ = -1;
  int64_t spawnedAt_ = 0;
  int64_t readyAt_ = 0;
  unsigned spawns_ = 0;
  unsigned failures_ = 0;
  unsigned prepareWaits_ = 0;
  unsigned badMessages_ = 0;
  unsigned sendErrors_ = 0;
  unsigned scanNotices_ = 0;
  std::string runtimeVersion_;
  std::string ntpServer_, hostname_;
  bool ntpKnown_ = false, hostnameKnown_ = false;
  std::string lastExit_;

  pid_t helperPid_ = -1;
  int64_t helperStopAt_ = -1;
  bool helperTermSent_ = false;
  bool helperKilled_ = false;
  bool helperStuck_ = false;
  unsigned helperStarts_ = 0;
  std::string helperState_ = "disabled";
  std::string helperLastExit_;
  // Scan completions may arrive after this object is gone; they hold only a weak reference.
  std::shared_ptr<char> alive_ = std::make_shared<char>(0);
  MicrophoneRelay microphone_;
};

}
}
