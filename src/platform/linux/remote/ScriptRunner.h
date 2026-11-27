#pragma once

#include <atomic>
#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <system_error>
#include <vector>

#include "core/ApplicationRuntime.h"
#include "core/FrameClock.h"
#include "core/PlatformDescriptor.h"
#include "core/ScreenDisplay.h"
#include "core/SettingsSaver.h"
#include "core/apps/DefaultApps.h"
#include "core/audio/AnalysisSource.h"
#include "core/script/ScriptService.h"
#include "core/script/ScriptSlots.h"
#include "media/ScriptIcon.h"
#include "persistence/AssetProbe.h"
#include "persistence/DeviceConfig.h"
#include "persistence/ScriptStore.h"
#include "platform/host/Files.h"
#include "platform/linux/LinuxBoard.h"
#include "platform/linux/LinuxCapabilities.h"
#include "platform/linux/LinuxDeviceFacts.h"
#include "platform/linux/LinuxScriptHttp.h"
#include "platform/linux/LinuxTarget.h"
#include "platform/linux/host/HostButtonInput.h"
#include "platform/linux/host/HostHttpServer.h"
#include "platform/linux/host/HostScriptFiles.h"
#include "platform/linux/images/HttpPictureLoader.h"
#include "platform/linux/images/RemoteImageStore.h"
#include "platform/linux/images/RemotePageIcon.h"
#include "platform/linux/oauth/OAuthApi.h"
#include "platform/linux/oauth/OAuthScripting.h"
#include "platform/linux/oauth/OAuthService.h"
#include "platform/linux/oauth/OAuthTransport.h"
#include "platform/linux/oauth/OAuthVault.h"
#include "platform/linux/remote/FramePacer.h"
#include "platform/linux/remote/RemoteClockApplication.h"
#include "platform/linux/remote/RemoteRole.h"
#include "platform/linux/remote/RunnerOptions.h"
#include "platform/linux/remote/ZoneClock.h"
#include "platform/linux/render/PageZoom.h"
#include "platform/linux/script/ExtensionHost.h"
#include "platform/linux/script/PerScriptStore.h"
#include "platform/linux/script/TcpScripting.h"
#include "platform/linux/secrets/SecretApi.h"
#include "platform/linux/secrets/SecretScripting.h"
#include "platform/linux/secrets/SecretService.h"
#include "system/MonotonicClock.h"
#include "transport/ScriptMqttBridge.h"
#include "transport/mqtt/ScriptOnlyMqtt.h"
#include "transport/net/HostResolver.h"

namespace awtrix::remote {

class ScriptRunner {
 public:
  explicit ScriptRunner(RunnerOptions options) : o_(std::move(options)) {}
  ~ScriptRunner() { zone_.store(nullptr); }
  ScriptRunner(const ScriptRunner&) = delete;
  ScriptRunner& operator=(const ScriptRunner&) = delete;

  int start();
  void run();
  int stop();
  bool silent() const { return silent_; }

 private:
  class System final : public ISystemService {
   public:
    void reboot() override { stopping_ = true; }
    void sleep(uint64_t) override { stopping_ = true; }
    void factoryReset() override { resetAll = true; stopping_ = true; }
    void resetSettings() override { resetUserSettings = true; stopping_ = true; }
    bool resetAll = false, resetUserSettings = false;
  };

  int openData();
  int loadConfig();
  int composeEngine();
  int beginRole();
  void composeAccess();
  void composeCapabilities();
  void composeScriptServices();
  void startScripts();
  int startServices();

  bool runFrame(int64_t now);
  void saveDue(int64_t now);
  bool persist();
  bool eraseAll(std::error_code& error);

  void applyConfig();
  net::FillRequest fillSecrets();
  CoreEngine& engine() { return app_->engine(); }

  static void logClock(std::time_t now, std::tm& out);
  static inline std::atomic<bool> stopping_{false};
  static inline std::atomic<const ZoneClock*> zone_{nullptr};

  using SavedScripts = ScriptStore<host::ScriptFiles>;

  RunnerOptions o_;
  DefaultApps apps_;
  LinuxTargetPolicy target_;
  std::string uid_;
  HostHttpOptions httpOptions_;
  os::FileLock lock_;
  DeviceConfig cfg_;
  std::unique_ptr<LinuxBoard> board_;
  ScreenDisplay display_;
  System system_;
  ZoneClock clock_;
  std::optional<images::HttpPictureLoader> pictureLoader_;
  std::optional<images::RemoteImageStore> remoteImages_;
  std::optional<RemotePageIcon> iconA_, iconB_;
  std::optional<PageZoom> pageZoom_;
  sound::AudioRouter audio_;
  AssetProbe assets_;
  std::optional<ApplicationServices> services_;
  std::optional<ApplicationRuntime> app_;
  std::optional<LinuxDeviceFacts> deviceFacts_;
  std::optional<LinuxCapabilities> capabilities_;
  RemoteRole role_;
  std::optional<RemoteClockApplication> application_;
  HostHttpServer http_;
  std::unique_ptr<net::IHostResolver> resolver_;
  std::optional<PlatformDescriptor> platform_;
  SavedScripts store_;
  ScriptIcon scriptIcon_;
  ScriptMqttBridge scriptMqtt_;
  script::ScriptServices scriptServices_;
  oauth::HttplibTransport oauthTransport_;
  std::optional<oauth::Vault> oauthVault_;
  std::optional<oauth::Service> oauthService_;
  std::optional<oauth::OAuthScripting> oauthScripting_;
  std::optional<oauth::Api> oauthApi_;
  std::optional<secrets::Service> secrets_;
  std::optional<secrets::SecretScripting> secretScripting_;
  std::optional<secrets::Api> secretApi_;
  script::PerScriptStores scriptStores_;
  audio::AnalysisRouter analysis_{nullptr, nullptr};
  linux_script::TcpScripting tcp_;
  std::vector<script::ScriptExtension*> extensions_;
  script::ScriptSlots scriptSlots_;
  script::ScriptHost* scripts_ = nullptr;
  std::unique_ptr<script::ExtensionHost> scriptExtensions_;
  LinuxScriptHttp scriptHttp_{fillSecrets()};
  HostButtonInput buttons_;
  SettingsSaver settingsSaver_;
  FramePacer pacer_{kFramePeriodMs};
  bool dirty_ = false;
  bool silent_ = false;
  std::unique_ptr<ScriptOnlyMqtt> scriptOnlyMqtt_;
};

}
