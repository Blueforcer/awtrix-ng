#pragma once

#include <sys/types.h>

#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

#include "platform/tc002/daemon/ControlRegistry.h"
#include "platform/tc002/daemon/Service.h"
#include "platform/tc002/daemon/wifi/CredentialStore.h"
#include "platform/tc002/daemon/wifi/WifiSystem.h"
#include "platform/tc002/daemon/wifi/WifiScan.h"
#include "platform/tc002/daemon/wifi/SupplicantSession.h"

namespace awtrix {
namespace tc002d {

struct WifiOptions {
  std::string stateDir, runDir;
  // The release's static wpa_supplicant.
  std::string supplicantPath;
  // The release's aic8800_bsp.ko and aic8800_fdrv.ko, rebuilt so that they never log key
  // material; loaded only when wlan0 does not exist yet.
  std::string moduleDirectory;
  // The developer flag file leaves root ADB over TCP as it is.
  bool keepAdbTcp = false;
};

// Owns the Wi-Fi link: USB-only ADB, stock supplicant stopped, aic8800 driver, our own
// wpa_supplicant child with a generated config, link state from its control interface.
// No supplicant runs while ADB may still listen on TCP, unless keepAdbTcp asks for exactly that.
// Control commands: "wifi-set", "wifi-status", "wifi-scan", "wifi-scan-results".
class WifiService : public Service, public WifiControl {
 public:
  WifiService(DeviceState& state, WifiOptions options, ControlRegistry& registry);
  WifiService(DeviceState& state, WifiOptions options, ControlRegistry& registry,
              std::unique_ptr<wifi::WifiSystem> system);
  ~WifiService() override;

  const char* name() const override { return "wifi"; }
  bool start(int64_t nowMs) override;
  void pollInterest(std::vector<PollInterest>& out) const override;
  void onReady(int fd, short revents, int64_t nowMs) override;
  int64_t nextDeadlineMs() const override;
  void onTime(int64_t nowMs) override;
  bool onChildExit(pid_t pid, int status, int64_t nowMs) override;
  void requestStop(int64_t nowMs) override;
  bool stopped() const override;

  // An empty password for the stored SSID keeps the stored key and reconnects (the web UI never
  // holds the old password); an empty password for any other SSID means an open network.
  bool setCredentials(const tc002::WifiCredentials& credentials) override;
  void eraseCredentials() override;
  void scan(ScanDone done) override;

  // Secret-free snapshot for status replies; never contains the passphrase or PSK.
  std::string statusJson() const;
  std::string scanResultsJson() const;
  std::string handleSetCommand(std::string_view payload);
  // Stored credentials, readable or not: the device is expected to come up on a network.
  bool configured() const { return haveProfile_ || storeStatus_ != wifi::StoreStatus::Missing; }

 private:
  enum class Step : uint8_t {
    Idle, AdbDisableTcp, AdbStopDaemon, AdbAwaitStopped, AdbStartDaemon, AdbAwaitStarted, AdbSettle,
    StockStop, StockAwaitStopped, LoadBsp, LoadFdrv, AwaitInterface, Unconfigured,
    SupplicantRunning, SupplicantRestarting, Backoff, Stopping, Stopped
  };
  enum class Stage : uint8_t { Adb, Stock, Modules, Supplicant };
  enum class AdbPolicy : uint8_t { NotRun, Pending, UsbOnly, TcpListening, Kept, Failed };
  using Pending = wifi::SupplicantSession::Command;

  static const char* stepName(Step step);
  static const char* adbPolicyName(AdbPolicy policy);

  void setStep(Step step, int64_t deadline = -1, int64_t probe = -1);
  void enterStage(Stage stage);
  void beginAdb();
  void checkAdbListeners();
  void finishAdb(AdbPolicy result, const char* detail);
  void beginStock();
  void probeStock();
  void beginModules();
  void interfaceReady();
  void hideKernelLog();
  void beginSupplicant();
  void beginAccessPoint();
  void linkTimers();
  void fail(Stage retry, const std::string& reason);
  void helperExited(bool ok, int code);
  void supplicantExited(int status);
  void persistExited(int status);
  bool launchPersist();
  bool credentialsPending() const;
  bool reloadProfile();
  void applyProfile();
  void restartSupplicant();

  void openControl();
  void closeControl();
  void scheduleReconnect();
  void pumpCommands();
  void handleReply(Pending kind, const std::string& reply);
  void handleEvent(const std::string& message);
  void controlTimers();
  bool commandWanted() const;
  void startScan();
  void finishScan(std::vector<tc002::WifiNetwork> networks, bool fresh);
  void probeIfHidden();
  bool profileVisible() const;
  void scanTimers();
  void eraseStore();

  void setLink(tc002::WifiLink link);
  void publish();
  std::vector<wifi::ProcessInfo> foreignSupplicants();
  void logEvent(const char* format, ...) const __attribute__((format(printf, 2, 3)));

  DeviceState& state_;
  WifiOptions options_;
  std::unique_ptr<wifi::WifiSystem> system_;
  wifi::CredentialStore store_;
  std::string wpaDirectory_, controlDirectory_, configPath_;

  wifi::WifiProfile profile_;
  bool haveProfile_ = false;
  wifi::StoreStatus storeStatus_ = wifi::StoreStatus::Missing;
  bool pendingCredentials_ = false;
  std::string pendingSsid_, pendingPassword_, requestedSsid_;

  Step step_ = Step::Idle;
  int64_t now_ = 0;
  int64_t stepDeadline_ = -1;
  int64_t nextProbe_ = -1;
  pid_t helper_ = -1;
  pid_t supplicant_ = -1;
  pid_t persist_ = -1;
  AdbPolicy adb_ = AdbPolicy::NotRun;
  std::string adbDetail_;
  unsigned adbStartAttempts_ = 0;
  unsigned stockEscalation_ = 0;
  Stage retryStage_ = Stage::Adb;
  int64_t retryAt_ = -1;
  unsigned failures_ = 0;
  int64_t supplicantStartedAt_ = -1;
  unsigned supplicantRestarts_ = 0;
  std::string storeError_;
  std::string linkError_;
  std::string driver_;
  bool stopping_ = false;
  int64_t stopDeadline_ = -1;

  wifi::SupplicantSession session_;
  bool wantStatus_ = false, wantSignal_ = false;
  int64_t nextStatus_ = -1, nextSignal_ = -1;

  bool accessPoint_ = false;
  bool wantAccessPointClients_ = false;
  int64_t stationDeadline_ = -1, accessPointRetryAt_ = -1, accessPointClientAt_ = -1, applyProfileAt_ = -1;
  int64_t accessPointEmptyAt_ = -1;
  int64_t addressDeadline_ = -1;
  bool probeHidden_ = false;
  wifi::WifiScan scan_;
  bool eraseRequested_ = false;

  tc002::WifiLink link_ = tc002::WifiLink::Unconfigured;
  std::string ssid_;
  int rssi_ = 0;
  std::string mac_;
};

}
}
