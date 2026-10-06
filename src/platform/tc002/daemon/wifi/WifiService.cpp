#include "platform/tc002/daemon/wifi/WifiService.h"

#include <poll.h>
#include <signal.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <unistd.h>

#include <cerrno>
#include <cstdarg>
#include <cstdio>
#include <cstring>

#include "core/api/JsonReader.h"
#include "core/api/JsonWriter.h"
#include "platform/tc002/daemon/Log.h"
#include "platform/tc002/daemon/ip/Hostname.h"
#include "platform/tc002/daemon/wifi/SupplicantConfig.h"

namespace awtrix {
namespace tc002d {
namespace {

using tc002::WifiLink;

constexpr char kQuietDriver[] = "aicwf_dbg_level=0";
constexpr char kDriverDebugLevel[] = "/sys/module/aic8800_fdrv/parameters/aicwf_dbg_level";
constexpr char kDmesgRestrict[] = "/proc/sys/kernel/dmesg_restrict";
constexpr unsigned kAdbPort = 5555;
constexpr int64_t kHelperTimeoutMs = 5000;
constexpr int64_t kModuleTimeoutMs = 30000;
constexpr int64_t kProbeIntervalMs = 100;
constexpr int64_t kInterfaceTimeoutMs = 10000;
constexpr int64_t kAdbSettleMs = 1500;
constexpr int64_t kStatusIntervalMs = 30000;
constexpr int64_t kSignalIntervalMs = 10000;
constexpr int64_t kStopGraceMs = 3000;
constexpr int64_t kStableRunMs = 120000;
constexpr int64_t kMaxBackoffMs = 60000;
constexpr int64_t kStationAttemptMs = 15000;
constexpr int64_t kAccessPointRetryMs = 60000;
constexpr int64_t kAccessPointIdleMs = 30000;
constexpr int64_t kProvisionReplyMs = 1000;
constexpr int64_t kAddressGraceMs = 30000;

void eraseString(std::string& text) {
  if (!text.empty()) wifi::secureErase(&text[0], text.size());
  text.clear();
}

bool validCredentials(const std::string& ssid, const std::string& password, const char** error) {
  switch (tc002::checkWifiCredentials(ssid, password)) {
    case tc002::WifiCredentialsProblem::None: return true;
    case tc002::WifiCredentialsProblem::Ssid: *error = "invalid-ssid"; return false;
    case tc002::WifiCredentialsProblem::Password: *error = "invalid-password"; return false;
  }
  return false;
}

bool exitedCleanly(int status) { return WIFEXITED(status) && WEXITSTATUS(status) == 0; }

// A forked task exits with the errno of what failed; a program with its own exit code.
std::string describeTaskExit(int status) {
  if (WIFEXITED(status)) {
    const int code = WEXITSTATUS(status);
    return code > 0 && code < 128 ? std::strerror(code) : "exit " + std::to_string(code);
  }
  if (WIFSIGNALED(status)) return "signal " + std::to_string(WTERMSIG(status));
  return "unknown";
}

std::string describeProgramExit(int status) {
  if (WIFEXITED(status)) return "exit " + std::to_string(WEXITSTATUS(status));
  if (WIFSIGNALED(status)) return "signal " + std::to_string(WTERMSIG(status));
  return "unknown";
}

void earliest(int64_t& best, int64_t candidate) {
  if (candidate >= 0 && (best < 0 || candidate < best)) best = candidate;
}

}

WifiService::WifiService(DeviceState& state, WifiOptions options, ControlRegistry& registry)
    : WifiService(state, std::move(options), registry, wifi::nativeWifiSystem()) {}

WifiService::WifiService(DeviceState& state, WifiOptions options, ControlRegistry& registry,
                         std::unique_ptr<wifi::WifiSystem> system)
    : state_(state),
      options_(std::move(options)),
      system_(std::move(system)),
      store_(options_.stateDir),
      wpaDirectory_(options_.runDir + "/wpa"),
      controlDirectory_(wpaDirectory_ + "/ctrl"),
      configPath_(wpaDirectory_ + "/wpa.conf") {
  registry.add("wifi-set", [this](std::string_view payload) { return handleSetCommand(payload); });
  registry.add("wifi-status", [this](std::string_view) { return statusJson(); });
  registry.add("wifi-scan", [this](std::string_view) {
    std::string out;
    api::JsonWriter json(out);
    now_ = system_->nowMs();
    if (stopping_) {
      json.beginObject().member("ok", false).member("error", "stopping").endObject();
      return out;
    }
    if (accessPoint_) {
      scan_.unavailable();
      json.beginObject().member("ok", false).member("error", scan_.error()).endObject();
      return out;
    }
    startScan();
    json.beginObject().member("ok", true).member("scanning", true).endObject();
    return out;
  });
  registry.add("wifi-scan-results", [this](std::string_view) { return scanResultsJson(); });
}

WifiService::~WifiService() {
  closeControl();
  eraseString(pendingPassword_);
}

bool WifiService::start(int64_t nowMs) {
  now_ = nowMs;
  if (!system_ || options_.stateDir.size() < 2 || options_.stateDir[0] != '/' || options_.runDir.size() < 2 ||
      options_.runDir[0] != '/' || options_.supplicantPath.size() < 2 || options_.supplicantPath[0] != '/' ||
      options_.moduleDirectory.size() < 2 || options_.moduleDirectory[0] != '/' ||
      controlDirectory_.size() + 16 > 100) {
    logEvent("invalid options");
    return false;
  }
  for (const std::string* directory : {&options_.runDir, &wpaDirectory_}) {
    if (mkdir(directory->c_str(), 0700) != 0 && errno != EEXIST) {
      logEvent("cannot create %s: %s", directory->c_str(), std::strerror(errno));
      return false;
    }
  }
  hideKernelLog();
  storeStatus_ = store_.load(profile_);
  haveProfile_ = storeStatus_ == wifi::StoreStatus::Loaded;
  if (storeStatus_ != wifi::StoreStatus::Loaded && storeStatus_ != wifi::StoreStatus::Missing)
    logEvent("credential store %s; waiting for new credentials", wifi::storeStatusName(storeStatus_));
  if (haveProfile_) {
    ssid_ = wifi::displaySsid(profile_.ssid);
    link_ = WifiLink::Connecting;
  }
  publish();
  enterStage(Stage::Adb);
  return true;
}

void WifiService::setStep(Step step, int64_t deadline, int64_t probe) {
  step_ = step;
  stepDeadline_ = deadline;
  nextProbe_ = probe;
  if (step != Step::Backoff) retryAt_ = -1;
}

void WifiService::enterStage(Stage stage) {
  switch (stage) {
    case Stage::Adb: beginAdb(); break;
    case Stage::Stock: beginStock(); break;
    case Stage::Modules: beginModules(); break;
    case Stage::Supplicant: beginSupplicant(); break;
  }
}

void WifiService::beginAdb() {
  if (options_.keepAdbTcp) {
    finishAdb(AdbPolicy::Kept, "");
    return;
  }
  adb_ = AdbPolicy::Pending;
  adbStartAttempts_ = 0;
  helper_ = system_->setProperty("service.adb.tcp.port", "-1");
  if (helper_ < 0) { finishAdb(AdbPolicy::Failed, "setprop spawn failed"); return; }
  setStep(Step::AdbDisableTcp, now_ + kHelperTimeoutMs);
}

void WifiService::checkAdbListeners() {
  const int listeners = system_->tcpListeners(kAdbPort);
  if (listeners == 0) { finishAdb(AdbPolicy::UsbOnly, ""); return; }
  if (listeners < 0) { finishAdb(AdbPolicy::Failed, "tcp table unreadable"); return; }
  helper_ = system_->setProperty("ctl.stop", "adbd");
  if (helper_ < 0) { finishAdb(AdbPolicy::Failed, "setprop spawn failed"); return; }
  setStep(Step::AdbStopDaemon, now_ + kHelperTimeoutMs);
}

void WifiService::finishAdb(AdbPolicy result, const char* detail) {
  adb_ = result;
  adbDetail_ = detail;
  if (result == AdbPolicy::UsbOnly) logEvent("adb: usb only, no tcp listener");
  else if (result == AdbPolicy::Kept)
    logEvent("adb: DEVELOPER MODE (developer flag file): root ADB over Wi-Fi left as it is");
  else logEvent("adb: %s %s", adbPolicyName(result), detail);
  beginStock();
}

std::vector<wifi::ProcessInfo> WifiService::foreignSupplicants() {
  std::vector<wifi::ProcessInfo> found = system_->processes("wpa_supplicant");
  std::vector<wifi::ProcessInfo> foreign;
  for (auto& process : found)
    if (process.pid != supplicant_) foreign.push_back(std::move(process));
  return foreign;
}

void WifiService::beginStock() {
  if (foreignSupplicants().empty()) { beginModules(); return; }
  logEvent("stopping the stock supplicant");
  helper_ = system_->setProperty("ctl.stop", "wpa_supplicant");
  if (helper_ < 0) { fail(Stage::Stock, "setprop spawn failed"); return; }
  setStep(Step::StockStop, now_ + kHelperTimeoutMs);
}

void WifiService::probeStock() {
  const std::vector<wifi::ProcessInfo> foreign = foreignSupplicants();
  if (foreign.empty()) { beginModules(); return; }
  if (now_ < stepDeadline_) { nextProbe_ = now_ + kProbeIntervalMs; return; }
  if (stockEscalation_ >= 2) { fail(Stage::Stock, "foreign supplicant survived SIGKILL"); return; }
  const int number = stockEscalation_ == 0 ? SIGTERM : SIGKILL;
  logEvent("signalling %zu foreign supplicant(s) with %d", foreign.size(), number);
  for (const auto& process : foreign) system_->signal(process.pid, number);
  ++stockEscalation_;
  setStep(Step::StockAwaitStopped, now_ + (stockEscalation_ == 1 ? 2000 : 1000), now_ + kProbeIntervalMs);
}

void WifiService::hideKernelLog() {
  const int restricted = system_->writeKernelAttribute(kDmesgRestrict, "1");
  const int cleared = system_->clearKernelLog();
  logEvent("kernel log: dmesg_restrict=1 %s%s, earlier messages %s%s",
           restricted ? "failed: " : "set", restricted ? std::strerror(restricted) : "",
           cleared ? "not cleared: " : "cleared", cleared ? std::strerror(cleared) : "");
}

void WifiService::beginModules() {
  if (system_->interfacePresent()) {
    if (driver_.empty()) {
      driver_ = "already loaded";
      logEvent("wlan0 exists; keeping the aic8800 driver that is loaded");
    }
    interfaceReady();
    return;
  }
  driver_ = "release";
  logEvent("loading the release aic8800 driver from %s", options_.moduleDirectory.c_str());
  helper_ = system_->loadModule(options_.moduleDirectory + "/aic8800_bsp.ko", "");
  if (helper_ < 0) { fail(Stage::Modules, "module loader spawn failed"); return; }
  setStep(Step::LoadBsp, now_ + kModuleTimeoutMs);
}

void WifiService::interfaceReady() {
  const int quiet = system_->writeKernelAttribute(kDriverDebugLevel, "0");
  if (quiet) logEvent("cannot set aicwf_dbg_level=0 (%s)", std::strerror(quiet));
  int error = 0;
  if (!system_->setInterfaceUp(error)) logEvent("wlan0 up failed: %s", std::strerror(error));
  mac_ = system_->interfaceAttribute("address");
  publish();
  beginSupplicant();
}

void WifiService::beginSupplicant() {
  if (adb_ != AdbPolicy::UsbOnly && adb_ != AdbPolicy::Kept) {
    fail(Stage::Adb, "adb over tcp not ruled out, staying unassociated");
    return;
  }
  if (!system_->interfacePresent()) { beginModules(); return; }
  if (!haveProfile_) accessPoint_ = true;
  if (accessPoint_ && scan_.active()) finishScan({}, false);
  probeHidden_ = !accessPoint_ && scan_.hasResults() && !profileVisible();
  std::string apName = state_.network().hostname;
  if (!ip::validHostname(apName)) apName = ip::hostnameForMac(mac_);
  if (apName.empty()) apName = "awtrixng";
  apName.resize(apName.size() > 32 ? 32 : apName.size());
  std::string config = accessPoint_ ? wifi::accessPointConfig(apName, controlDirectory_)
                                   : wifi::supplicantConfig(profile_, controlDirectory_, probeHidden_);
  int error = 0;
  const bool written = !config.empty() && wifi::writePrivateFile(configPath_, config, error);
  eraseString(config);
  if (!written) { fail(Stage::Supplicant, std::string("config write failed: ") + std::strerror(error)); return; }
  supplicant_ = system_->startSupplicant(options_.supplicantPath, configPath_);
  if (supplicant_ < 0) { fail(Stage::Supplicant, "supplicant spawn failed"); return; }
  logEvent("%s supplicant started (pid %d%s)", accessPoint_ ? "access-point" : "network", static_cast<int>(supplicant_),
           probeHidden_ ? ", probing for a hidden ssid" : "");
  setStep(Step::SupplicantRunning);
  supplicantStartedAt_ = now_;
  session_.start(now_);
  if (accessPoint_) {
    stationDeadline_ = -1;
    accessPointRetryAt_ = haveProfile_ ? now_ + kAccessPointRetryMs : -1;
    accessPointClientAt_ = now_;
    ssid_ = apName;
    setLink(WifiLink::Unconfigured);
    return;
  }
  if (stationDeadline_ < 0) stationDeadline_ = now_ + kStationAttemptMs;
  ssid_ = wifi::displaySsid(profile_.ssid);
  if (link_ != WifiLink::Failed) setLink(WifiLink::Connecting);
  else publish();
}

void WifiService::beginAccessPoint() {
  accessPoint_ = true;
  stationDeadline_ = -1;
  addressDeadline_ = -1;
  applyProfileAt_ = -1;
  if (scan_.active()) finishScan({}, false);
  setLink(WifiLink::Unconfigured);
  restartSupplicant();
}

void WifiService::linkTimers() {
  if (!accessPoint_ && link_ == WifiLink::Connected) {
    const std::string& address = state_.network().ipv4;
    if (!address.empty() && address != "0.0.0.0") addressDeadline_ = -1;
    else if (addressDeadline_ < 0) addressDeadline_ = now_ + kAddressGraceMs;
    else if (now_ >= addressDeadline_) {
      logEvent("router did not provide an address; starting the setup access point");
      beginAccessPoint();
      return;
    }
  }
  if (accessPointEmptyAt_ >= 0 && now_ >= accessPointEmptyAt_) {
    accessPointEmptyAt_ = -1;
    if (accessPoint_ && session_.attached() && link_ == WifiLink::AccessPoint && haveProfile_ &&
        applyProfileAt_ < 0 && !credentialsPending() && now_ - accessPointClientAt_ >= kAccessPointIdleMs) {
      accessPoint_ = false;
      applyProfile();
      return;
    }
  }
  if (applyProfileAt_ >= 0 && now_ >= applyProfileAt_) {
    applyProfileAt_ = -1;
    if (credentialsPending()) return;
    accessPoint_ = false;
    applyProfile();
    return;
  }
  if (!accessPoint_ && step_ == Step::SupplicantRunning && stationDeadline_ >= 0 && now_ >= stationDeadline_) {
    logEvent("router unavailable; starting the setup access point");
    beginAccessPoint();
    return;
  }
  if (accessPoint_ && haveProfile_ && link_ == WifiLink::AccessPoint && accessPointRetryAt_ >= 0 &&
      now_ >= accessPointRetryAt_) {
    accessPointRetryAt_ = now_ + kAccessPointRetryMs;
    if (session_.attached() && applyProfileAt_ < 0 && !credentialsPending() &&
        now_ - accessPointClientAt_ >= kAccessPointIdleMs) wantAccessPointClients_ = true;
  }
}

void WifiService::fail(Stage retry, const std::string& reason) {
  linkError_ = reason;
  ++failures_;
  const unsigned shift = failures_ > 7 ? 6 : failures_ - 1;
  int64_t delay = int64_t(1000) << shift;
  if (delay > kMaxBackoffMs) delay = kMaxBackoffMs;
  logEvent("%s; retrying in %lld ms", reason.c_str(), static_cast<long long>(delay));
  setStep(Step::Backoff);
  retryStage_ = retry;
  retryAt_ = now_ + delay;
}

void WifiService::helperExited(bool ok, int status) {
  switch (step_) {
    case Step::AdbDisableTcp:
      if (ok) checkAdbListeners();
      else finishAdb(AdbPolicy::Failed, "setting service.adb.tcp.port failed");
      break;
    case Step::AdbStopDaemon:
      if (!ok) { finishAdb(AdbPolicy::Failed, "ctl.stop adbd failed"); break; }
      setStep(Step::AdbAwaitStopped, now_ + kHelperTimeoutMs, now_ + kProbeIntervalMs);
      break;
    case Step::AdbStartDaemon:
      if (!ok) { finishAdb(AdbPolicy::Failed, "ctl.start adbd failed"); break; }
      setStep(Step::AdbAwaitStarted, now_ + kHelperTimeoutMs, now_ + kProbeIntervalMs);
      break;
    case Step::StockStop:
      stockEscalation_ = 0;
      setStep(Step::StockAwaitStopped, now_ + 3000, now_ + kProbeIntervalMs);
      break;
    case Step::LoadBsp:
      if (!ok) { fail(Stage::Modules, "aic8800_bsp.ko: " + describeTaskExit(status)); break; }
      helper_ = system_->loadModule(options_.moduleDirectory + "/aic8800_fdrv.ko", kQuietDriver);
      if (helper_ < 0) { fail(Stage::Modules, "module loader spawn failed"); break; }
      setStep(Step::LoadFdrv, now_ + kModuleTimeoutMs);
      break;
    case Step::LoadFdrv:
      if (!ok) { fail(Stage::Modules, "aic8800_fdrv.ko: " + describeTaskExit(status)); break; }
      setStep(Step::AwaitInterface, now_ + kInterfaceTimeoutMs, now_);
      break;
    default:
      break;
  }
}

bool WifiService::onChildExit(pid_t pid, int status, int64_t nowMs) {
  now_ = nowMs;
  if (pid <= 0) return false;
  if (pid == helper_) {
    helper_ = -1;
    stepDeadline_ = -1;
    if (!stopping_) helperExited(exitedCleanly(status), status);
    return true;
  }
  if (pid == supplicant_) {
    supplicantExited(status);
    return true;
  }
  if (pid == persist_) {
    persistExited(status);
    return true;
  }
  return false;
}

void WifiService::supplicantExited(int status) {
  supplicant_ = -1;
  addressDeadline_ = -1;
  closeControl();
  if (stopping_) {
    unlink(configPath_.c_str());
    return;
  }
  if (step_ == Step::SupplicantRestarting) {
    beginSupplicant();
    return;
  }
  ++supplicantRestarts_;
  if (supplicantStartedAt_ >= 0 && now_ - supplicantStartedAt_ >= kStableRunMs) failures_ = 0;
  rssi_ = 0;
  if (link_ != WifiLink::Failed) link_ = WifiLink::Disconnected;
  publish();
  fail(Stage::Supplicant, "supplicant exited: " + describeProgramExit(status));
}

void WifiService::persistExited(int status) {
  persist_ = -1;
  if (eraseRequested_) {
    eraseRequested_ = false;
    eraseStore();
  } else {
    const bool stored = exitedCleanly(status);
    if (stored) {
      storeError_.clear();
      logEvent("credentials stored");
    } else {
      storeError_ = "credential store write failed: " + describeTaskExit(status);
      logEvent("%s", storeError_.c_str());
    }
    if (reloadProfile() || stored) applyProfile();
  }
  launchPersist();
  publish();
}

bool WifiService::reloadProfile() {
  wifi::WifiProfile loaded;
  storeStatus_ = store_.load(loaded);
  if (storeStatus_ != wifi::StoreStatus::Loaded) return false;
  const bool changed = !haveProfile_ || loaded.ssid != profile_.ssid || loaded.open != profile_.open ||
                       loaded.psk != profile_.psk;
  profile_ = loaded;
  haveProfile_ = true;
  return changed;
}

void WifiService::applyProfile() {
  if (stopping_) return;
  if (accessPoint_ && step_ == Step::SupplicantRunning) {
    applyProfileAt_ = credentialsPending() ? -1 : now_ + kProvisionReplyMs;
    publish();
    return;
  }
  accessPoint_ = false;
  accessPointRetryAt_ = -1;
  wantAccessPointClients_ = false;
  accessPointEmptyAt_ = -1;
  stationDeadline_ = now_ + kStationAttemptMs;
  addressDeadline_ = -1;
  scan_.clearError();
  failures_ = 0;
  ssid_ = wifi::displaySsid(profile_.ssid);
  rssi_ = 0;
  link_ = WifiLink::Connecting;
  publish();
  switch (step_) {
    case Step::Unconfigured: beginSupplicant(); break;
    case Step::SupplicantRunning: restartSupplicant(); break;
    case Step::Backoff:
      if (retryStage_ == Stage::Supplicant) retryAt_ = now_;
      break;
    default: break;
  }
}

void WifiService::restartSupplicant() {
  if (supplicant_ < 0) { beginSupplicant(); return; }
  closeControl();
  system_->signal(supplicant_, SIGTERM);
  setStep(Step::SupplicantRestarting, now_ + kStopGraceMs);
}

void WifiService::eraseStore() {
  int error = 0;
  storeStatus_ = wifi::StoreStatus::Missing;
  if (store_.erase(error)) return;
  storeStatus_ = wifi::StoreStatus::IoError;
  storeError_ = std::string("credential erase failed: ") + std::strerror(error);
  logEvent("%s", storeError_.c_str());
}

void WifiService::eraseCredentials() {
  now_ = system_->nowMs();
  storeError_.clear();
  eraseString(pendingPassword_);
  pendingSsid_.clear();
  pendingCredentials_ = false;
  haveProfile_ = false;
  applyProfileAt_ = -1;
  stationDeadline_ = -1;
  accessPointRetryAt_ = -1;
  profile_ = wifi::WifiProfile();
  if (persist_ >= 0) {
    eraseRequested_ = true;
    system_->signal(persist_, SIGKILL);
  } else {
    eraseStore();
  }
  unlink(configPath_.c_str());
  logEvent("credentials erased");
  if (stopping_) return;
  failures_ = 0;
  ssid_.clear();
  rssi_ = 0;
  setLink(WifiLink::Unconfigured);
  if (step_ == Step::SupplicantRunning) beginAccessPoint();
  else if (step_ == Step::Backoff && retryStage_ == Stage::Supplicant) retryAt_ = now_;
}

void WifiService::scan(ScanDone done) {
  now_ = system_->nowMs();
  if (stopping_ || accessPoint_) {
    if (accessPoint_) scan_.unavailable();
    if (done) done(scan_.networks());
    return;
  }
  scan_.wait(std::move(done));
  startScan();
}

void WifiService::startScan() {
  if (accessPoint_ || !scan_.start(now_)) return;
  if (step_ == Step::Unconfigured) beginSupplicant();
}

void WifiService::finishScan(std::vector<tc002::WifiNetwork> networks, bool fresh) {
  scan_.finish(std::move(networks), fresh, now_);
  if (fresh) probeIfHidden();
}

void WifiService::probeIfHidden() {
  if (accessPoint_ || probeHidden_ || step_ != Step::SupplicantRunning || link_ == WifiLink::Connected ||
      profileVisible())
    return;
  logEvent("configured network not in the scan results; probing for it as a hidden ssid");
  restartSupplicant();
}

bool WifiService::profileVisible() const { return scan_.contains(profile_.ssid); }

void WifiService::scanTimers() {
  if (scan_.timedOut(now_)) {
    logEvent("scan did not complete; answering with the previous results");
    finishScan({}, false);
  }
  scan_.onTime(now_);
}

bool WifiService::setCredentials(const tc002::WifiCredentials& credentials) {
  now_ = system_->nowMs();
  const char* error = nullptr;
  if (stopping_ || !validCredentials(credentials.ssid, credentials.password, &error)) return false;
  applyProfileAt_ = -1;
  storeError_.clear();
  if (credentials.password.empty() && haveProfile_ && !profile_.open && credentials.ssid == profile_.ssid) {
    logEvent("same network without a password: keeping the stored key");
    applyProfile();
    return true;
  }
  pendingSsid_ = credentials.ssid;
  eraseString(pendingPassword_);
  pendingPassword_ = credentials.password;
  pendingCredentials_ = true;
  requestedSsid_ = wifi::displaySsid(credentials.ssid);
  const bool launched = launchPersist();
  publish();
  return launched;
}

bool WifiService::credentialsPending() const {
  return !stopping_ && (pendingCredentials_ || (persist_ >= 0 && !eraseRequested_));
}

bool WifiService::launchPersist() {
  if (persist_ >= 0 || !pendingCredentials_) return true;
  pendingCredentials_ = false;
  const std::string ssid = std::move(pendingSsid_);
  std::string secret;
  secret.swap(pendingPassword_);
  const std::string directory = options_.stateDir;
  persist_ = system_->runTask([&]() -> int {
    wifi::WifiProfile profile;
    if (!wifi::makeProfile(ssid, secret, profile)) return EINVAL;
    int error = 0;
    return wifi::CredentialStore(directory).save(profile, error) ? 0 : (error ? error : EIO);
  });
  eraseString(secret);
  if (persist_ >= 0) return true;
  storeError_ = "credential writer spawn failed";
  logEvent("%s", storeError_.c_str());
  return false;
}

void WifiService::openControl() {
  if (!session_.open(wpaDirectory_, controlDirectory_ + "/wlan0", now_)) {
    scheduleReconnect();
    return;
  }
  accessPointEmptyAt_ = -1;
  wantStatus_ = true;
  nextStatus_ = now_ + kStatusIntervalMs;
}

void WifiService::closeControl() {
  session_.close();
  accessPointEmptyAt_ = -1;
  wantStatus_ = wantSignal_ = false;
  nextSignal_ = -1;
  scan_.resetControl();
  wantAccessPointClients_ = false;
}

void WifiService::scheduleReconnect() {
  closeControl();
  session_.scheduleRetry(now_);
}

void WifiService::pumpCommands() {
  if (!session_.ready()) return;
  const char* text = nullptr;
  Pending kind = Pending::None;
  if (wantStatus_) { text = "STATUS"; kind = Pending::Status; wantStatus_ = false; }
  else if (wantAccessPointClients_) {
    text = "STA-FIRST"; kind = Pending::AccessPointClients; wantAccessPointClients_ = false;
  }
  else if (scan_.commandWanted()) { text = "SCAN"; kind = Pending::Scan; }
  else if (scan_.resultsWanted()) { text = "SCAN_RESULTS"; kind = Pending::ScanResults; scan_.takeResults(); }
  else if (wantSignal_) { text = "SIGNAL_POLL"; kind = Pending::SignalPoll; wantSignal_ = false; }
  else return;
  if (!session_.send(kind, text, now_)) {
    scheduleReconnect();
    return;
  }
}

void WifiService::handleReply(Pending kind, const std::string& reply) {
  if (kind == Pending::AccessPointClients) {
    if (accessPoint_ && session_.attached() && link_ == WifiLink::AccessPoint && haveProfile_ &&
        applyProfileAt_ < 0 && !credentialsPending() && now_ - accessPointClientAt_ >= kAccessPointIdleMs &&
        reply == "FAIL\n") {
      accessPointEmptyAt_ = now_ + kProvisionReplyMs;
    }
    return;
  }
  if (kind == Pending::Scan) {
    scan_.commandReply(reply, now_);
    return;
  }
  if (kind == Pending::ScanResults) {
    finishScan(wifi::parseScanResults(reply), true);
    return;
  }
  if (kind == Pending::SignalPoll) {
    int rssi = 0;
    if (wifi::parseSignalPoll(reply, rssi) && link_ == WifiLink::Connected) {
      rssi_ = rssi < -200 ? -200 : rssi > 0 ? 0 : rssi;
      publish();
    }
    return;
  }
  if (kind != Pending::Status) return;
  wifi::WpaStatus status;
  if (!wifi::parseStatus(reply, status)) return;
  const bool completed = status.wpaState == "COMPLETED";
  if (accessPoint_) {
    if (completed && status.mode == "AP") setLink(WifiLink::AccessPoint);
    else if (link_ == WifiLink::AccessPoint) setLink(WifiLink::Unconfigured);
    return;
  }
  if (completed && link_ != WifiLink::Connected) {
    setLink(WifiLink::Connected);
    wantSignal_ = true;
  } else if (!completed && link_ == WifiLink::Connected) {
    rssi_ = 0;
    setLink(WifiLink::Disconnected);
  }
}

void WifiService::handleEvent(const std::string& message) {
  const wifi::WpaEvent event = wifi::parseEvent(message);
  if (accessPoint_) {
    if (event.type == wifi::WpaEventType::AccessPointEnabled) setLink(WifiLink::AccessPoint);
    else if (event.type == wifi::WpaEventType::AccessPointDisabled) setLink(WifiLink::Unconfigured);
    else if (event.type == wifi::WpaEventType::StationConnected ||
             event.type == wifi::WpaEventType::StationDisconnected) {
      accessPointClientAt_ = now_;
      accessPointRetryAt_ = haveProfile_ ? now_ + kAccessPointRetryMs : -1;
      wantAccessPointClients_ = false;
      accessPointEmptyAt_ = -1;
    }
    return;
  }
  if (event.type == wifi::WpaEventType::ScanResults) {
    scan_.resultsEvent(!probeHidden_ && link_ != WifiLink::Connected);
    return;
  }
  switch (event.type) {
    case wifi::WpaEventType::Connected:
      failures_ = 0;
      setLink(WifiLink::Connected);
      wantStatus_ = true;
      wantSignal_ = true;
      break;
    case wifi::WpaEventType::Disconnected:
      rssi_ = 0;
      if (link_ != WifiLink::Failed) setLink(WifiLink::Disconnected);
      break;
    case wifi::WpaEventType::TemporarilyDisabled:
      rssi_ = 0;
      if (event.wrongKey) {
        if (link_ != WifiLink::Failed) logEvent("association rejected: wrong key");
        setLink(WifiLink::Failed);
      } else if (link_ != WifiLink::Failed) {
        setLink(WifiLink::Disconnected);
      }
      break;
    case wifi::WpaEventType::Terminating:
    case wifi::WpaEventType::ScanResults:
    case wifi::WpaEventType::AccessPointEnabled:
    case wifi::WpaEventType::AccessPointDisabled:
    case wifi::WpaEventType::StationConnected:
    case wifi::WpaEventType::StationDisconnected:
    case wifi::WpaEventType::Other:
      break;
  }
}

void WifiService::pollInterest(std::vector<PollInterest>& out) const { session_.pollInterest(out); }

void WifiService::onReady(int fd, short revents, int64_t nowMs) {
  now_ = nowMs;
  std::string message;
  if (session_.commandFd(fd)) {
    Pending kind = Pending::None;
    for (unsigned i = 0; i < 16 && session_.receiveReply(message, kind); ++i)
      if (kind != Pending::None) handleReply(kind, message);
    if ((revents & (POLLERR | POLLHUP | POLLNVAL)) && !(revents & POLLIN)) scheduleReconnect();
  } else if (session_.monitorFd(fd)) {
    for (unsigned i = 0; i < 32 && session_.receiveMonitor(message); ++i)
      if (!message.empty() && (message[0] == '<' || message.compare(0, 7, "IFNAME=") == 0))
        handleEvent(message);
    if ((revents & (POLLERR | POLLHUP | POLLNVAL)) && !(revents & POLLIN)) scheduleReconnect();
  }
  pumpCommands();
}

void WifiService::controlTimers() {
  if (!session_.isOpen()) {
    if (session_.gaveUp(now_)) {
      linkError_ = "supplicant control interface unavailable";
      logEvent("%s", linkError_.c_str());
      system_->signal(supplicant_, SIGKILL);
      return;
    }
    if (session_.retryDue(now_)) openControl();
    if (!session_.isOpen()) return;
  }
  Pending expired = Pending::None;
  using Timeout = wifi::SupplicantSession::Timeout;
  switch (session_.expire(now_, wpaDirectory_, controlDirectory_, expired)) {
    case Timeout::Unresponsive:
      linkError_ = "supplicant not answering";
      logEvent("%s", linkError_.c_str());
      closeControl();
      system_->signal(supplicant_, SIGKILL);
      return;
    case Timeout::Reconnect:
      scheduleReconnect();
      return;
    case Timeout::Reopened:
      if (expired == Pending::ScanResults) scan_.requestResults();
      break;
    case Timeout::None: break;
  }
  session_.attachTimer(now_);
  if (nextStatus_ >= 0 && now_ >= nextStatus_) {
    wantStatus_ = true;
    nextStatus_ = now_ + kStatusIntervalMs;
  }
  if (link_ == WifiLink::Connected) {
    if (nextSignal_ < 0) nextSignal_ = now_ + kSignalIntervalMs;
    else if (now_ >= nextSignal_) { wantSignal_ = true; nextSignal_ = now_ + kSignalIntervalMs; }
  } else {
    nextSignal_ = -1;
  }
  pumpCommands();
}

bool WifiService::commandWanted() const {
  return wantStatus_ || wantSignal_ || scan_.resultsWanted() || wantAccessPointClients_ || scan_.commandWanted();
}

int64_t WifiService::nextDeadlineMs() const {
  int64_t best = -1;
  earliest(best, stopDeadline_);
  if (stopping_) return best;
  earliest(best, stepDeadline_);
  earliest(best, nextProbe_);
  earliest(best, applyProfileAt_);
  earliest(best, accessPointEmptyAt_);
  earliest(best, addressDeadline_);
  if (step_ == Step::SupplicantRunning) earliest(best, stationDeadline_);
  if (link_ == WifiLink::AccessPoint) earliest(best, accessPointRetryAt_);
  earliest(best, scan_.nextDeadlineMs());
  if (step_ == Step::Backoff) earliest(best, retryAt_);
  if (step_ == Step::SupplicantRunning) {
    earliest(best, session_.nextDeadlineMs(now_, commandWanted()));
    if (session_.isOpen()) {
      earliest(best, nextStatus_);
      if (link_ == WifiLink::Connected) earliest(best, nextSignal_ < 0 ? now_ : nextSignal_);
    }
  }
  return best;
}

void WifiService::onTime(int64_t nowMs) {
  now_ = nowMs;
  if (stopping_) {
    if (stopDeadline_ >= 0 && now_ >= stopDeadline_) {
      stopDeadline_ = -1;
      system_->signal(helper_, SIGKILL);
      system_->signal(supplicant_, SIGKILL);
      system_->signal(persist_, SIGKILL);
    }
    return;
  }
  scanTimers();
  linkTimers();
  const bool probeDue = nextProbe_ >= 0 && now_ >= nextProbe_;
  const bool deadlineDue = stepDeadline_ >= 0 && now_ >= stepDeadline_;
  switch (step_) {
    case Step::AdbDisableTcp:
    case Step::AdbStopDaemon:
    case Step::AdbStartDaemon:
    case Step::StockStop:
    case Step::LoadBsp:
    case Step::LoadFdrv:
      if (deadlineDue) {
        stepDeadline_ = -1;
        logEvent("%s helper timed out", stepName(step_));
        system_->signal(helper_, SIGKILL);
      }
      break;
    case Step::AdbAwaitStopped:
      if (probeDue) {
        nextProbe_ = now_ + kProbeIntervalMs;
        if (system_->processes("adbd").empty()) {
          ++adbStartAttempts_;
          helper_ = system_->setProperty("ctl.start", "adbd");
          if (helper_ < 0) { finishAdb(AdbPolicy::Failed, "setprop spawn failed"); break; }
          setStep(Step::AdbStartDaemon, now_ + kHelperTimeoutMs);
          break;
        }
      }
      if (deadlineDue) finishAdb(AdbPolicy::Failed, "adbd did not stop");
      break;
    case Step::AdbAwaitStarted:
      if (probeDue) {
        nextProbe_ = now_ + kProbeIntervalMs;
        if (!system_->processes("adbd").empty()) {
          setStep(Step::AdbSettle, now_ + kAdbSettleMs);
          break;
        }
      }
      if (deadlineDue) {
        if (adbStartAttempts_ < 2) {
          ++adbStartAttempts_;
          helper_ = system_->setProperty("ctl.start", "adbd");
          if (helper_ < 0) { finishAdb(AdbPolicy::Failed, "setprop spawn failed"); break; }
          setStep(Step::AdbStartDaemon, now_ + kHelperTimeoutMs);
        } else {
          finishAdb(AdbPolicy::Failed, "adbd did not restart");
        }
      }
      break;
    case Step::AdbSettle:
      if (deadlineDue) {
        stepDeadline_ = -1;
        const int listeners = system_->tcpListeners(kAdbPort);
        if (listeners == 0) finishAdb(AdbPolicy::UsbOnly, "");
        else if (listeners > 0) finishAdb(AdbPolicy::TcpListening, "tcp 5555 still listening after restart");
        else finishAdb(AdbPolicy::Failed, "tcp table unreadable");
      }
      break;
    case Step::StockAwaitStopped:
      if (probeDue || deadlineDue) probeStock();
      break;
    case Step::AwaitInterface:
      if (probeDue) {
        nextProbe_ = now_ + 200;
        if (system_->interfacePresent()) {
          interfaceReady();
          break;
        }
      }
      if (deadlineDue) fail(Stage::Modules, "wlan0 did not appear");
      break;
    case Step::Backoff:
      if (retryAt_ >= 0 && now_ >= retryAt_) enterStage(retryStage_);
      break;
    case Step::SupplicantRestarting:
      if (deadlineDue) {
        stepDeadline_ = -1;
        system_->signal(supplicant_, SIGKILL);
      }
      break;
    case Step::SupplicantRunning:
      if (failures_ && supplicantStartedAt_ >= 0 && now_ - supplicantStartedAt_ >= kStableRunMs) failures_ = 0;
      controlTimers();
      break;
    case Step::Idle:
    case Step::Unconfigured:
    case Step::Stopping:
    case Step::Stopped:
      break;
  }
}

void WifiService::requestStop(int64_t nowMs) {
  now_ = nowMs;
  if (stopping_) return;
  stopping_ = true;
  if (scan_.active()) finishScan({}, false);
  applyProfileAt_ = stationDeadline_ = accessPointRetryAt_ = addressDeadline_ = -1;
  closeControl();
  session_.cancelRetry();
  setStep(Step::Stopping);
  system_->signal(helper_, SIGKILL);
  system_->signal(supplicant_, SIGTERM);
  stopDeadline_ = helper_ >= 0 || supplicant_ >= 0 || persist_ >= 0 ? now_ + kStopGraceMs : -1;
  eraseString(pendingPassword_);
  pendingCredentials_ = false;
  if (supplicant_ < 0) unlink(configPath_.c_str());
  rssi_ = 0;
  if (link_ != WifiLink::Unconfigured) link_ = WifiLink::Disconnected;
  publish();
}

bool WifiService::stopped() const {
  return helper_ < 0 && supplicant_ < 0 && persist_ < 0;
}

void WifiService::setLink(WifiLink link) {
  if (link != WifiLink::Connected) addressDeadline_ = -1;
  else if (link_ != WifiLink::Connected) addressDeadline_ = now_ + kAddressGraceMs;
  if (link == WifiLink::AccessPoint && link_ != WifiLink::AccessPoint) {
    accessPointClientAt_ = now_;
    accessPointRetryAt_ = haveProfile_ ? now_ + kAccessPointRetryMs : -1;
  }
  link_ = link;
  if (link == WifiLink::Connected) { linkError_.clear(); stationDeadline_ = -1; }
  else {
    rssi_ = 0;
    if (!accessPoint_ && haveProfile_ && stationDeadline_ < 0) stationDeadline_ = now_ + kStationAttemptMs;
  }
  publish();
}

void WifiService::publish() {
  tc002::NetworkStatus network = state_.network();
  const bool requested = credentialsPending() && !accessPoint_;
  const WifiLink link = requested ? WifiLink::Connecting : link_;
  const std::string& ssid = requested ? requestedSsid_ : ssid_;
  const int rssi = requested ? 0 : rssi_;
  if (network.link == link && network.ssid == ssid && network.rssi == rssi && network.mac == mac_) return;
  network.link = link;
  network.ssid = ssid;
  network.rssi = rssi;
  network.mac = mac_;
  state_.setNetwork(network);
}

std::string WifiService::statusJson() const {
  std::string out;
  api::JsonWriter json(out);
  const int listeners = step_ == Step::Idle ? -1 : system_->tcpListeners(kAdbPort);
  const bool requested = credentialsPending() && !accessPoint_;
  json.beginObject()
      .member("stage", stepName(step_))
      .member("link", tc002::wifiLinkName(requested ? WifiLink::Connecting : link_))
      .member("ssid", requested ? requestedSsid_ : ssid_)
      .member("rssi", requested ? 0 : rssi_)
      .member("mac", mac_)
      .member("configured", haveProfile_)
      .member("store", persist_ >= 0 ? "saving" : wifi::storeStatusName(storeStatus_))
      .member("adb", adbPolicyName(adb_))
      .member("adbDetail", adbDetail_)
      .member("adbTcpListeners", listeners)
      .member("supplicant", supplicant_ < 0 ? "none" : accessPoint_ ? "access-point" : "network")
      .member("driver", driver_)
      .member("scanning", scan_.active())
      .member("scanError", scan_.error())
      .member("supplicantRestarts", supplicantRestarts_)
      .member("error", storeError_.empty() ? linkError_ : storeError_)
      .endObject();
  return out;
}

std::string WifiService::scanResultsJson() const { return scan_.json(system_->nowMs()); }

std::string WifiService::handleSetCommand(std::string_view payload) {
  const auto reply = [](const char* error) {
    std::string out;
    api::JsonWriter json(out);
    json.beginObject().member("ok", false).member("error", error).endObject();
    return out;
  };
  if (payload.size() > 1024) return reply("malformed");
  api::JsonReader root(payload);
  if (!root.isObject()) return reply("malformed");
  const api::JsonReader ssidValue = api::memberValue(root, "ssid");
  const api::JsonReader passwordValue = api::memberValue(root, "password");
  tc002::WifiCredentials credentials;
  if (!ssidValue.isString() || !passwordValue.isString() || !ssidValue.appendString(credentials.ssid) ||
      !passwordValue.appendString(credentials.password)) {
    eraseString(credentials.password);
    return reply("malformed");
  }
  const char* error = nullptr;
  std::string result;
  if (adb_ == AdbPolicy::Pending) result = reply("busy");
  else if (!validCredentials(credentials.ssid, credentials.password, &error)) result = reply(error);
  else if (!setCredentials(credentials)) result = reply("rejected");
  eraseString(credentials.password);
  if (!result.empty()) return result;
  api::JsonWriter json(result);
  json.beginObject().member("ok", true).endObject();
  return result;
}

const char* WifiService::stepName(Step step) {
  switch (step) {
    case Step::Idle: return "idle";
    case Step::AdbDisableTcp: return "adb-disable-tcp";
    case Step::AdbStopDaemon: return "adb-stop";
    case Step::AdbAwaitStopped: return "adb-await-stopped";
    case Step::AdbStartDaemon: return "adb-start";
    case Step::AdbAwaitStarted: return "adb-await-started";
    case Step::AdbSettle: return "adb-settle";
    case Step::StockStop: return "stock-stop";
    case Step::StockAwaitStopped: return "stock-await-stopped";
    case Step::LoadBsp: return "load-bsp";
    case Step::LoadFdrv: return "load-fdrv";
    case Step::AwaitInterface: return "await-interface";
    case Step::Unconfigured: return "unconfigured";
    case Step::SupplicantRunning: return "running";
    case Step::SupplicantRestarting: return "restarting";
    case Step::Backoff: return "backoff";
    case Step::Stopping: return "stopping";
    case Step::Stopped: return "stopped";
  }
  return "unknown";
}

const char* WifiService::adbPolicyName(AdbPolicy policy) {
  switch (policy) {
    case AdbPolicy::NotRun: return "not-run";
    case AdbPolicy::Pending: return "pending";
    case AdbPolicy::UsbOnly: return "usb-only";
    case AdbPolicy::TcpListening: return "tcp-listening";
    case AdbPolicy::Kept: return "kept";
    case AdbPolicy::Failed: return "failed";
  }
  return "unknown";
}

void WifiService::logEvent(const char* format, ...) const {
  char text[256];
  va_list arguments;
  va_start(arguments, format);
  std::vsnprintf(text, sizeof text, format, arguments);
  va_end(arguments);
  Log::text("wifi", text);
}

}
}
