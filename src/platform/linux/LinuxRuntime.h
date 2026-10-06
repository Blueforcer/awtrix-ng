#pragma once

#include <csignal>
#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <system_error>
#include <vector>

#include "core/ApplicationRuntime.h"
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
#include "platform/linux/host/HostButtonInput.h"
#include "platform/linux/host/HostHttpServer.h"
#include "platform/linux/host/HostScriptFiles.h"
#include "platform/linux/LinuxBoard.h"
#include "platform/linux/LinuxButtonWebhook.h"
#include "platform/linux/LinuxCapabilities.h"
#include "platform/linux/LinuxDeviceFacts.h"
#include "platform/linux/LinuxLanLogin.h"
#include "platform/linux/LinuxMqttTls.h"
#include "platform/linux/LinuxOptions.h"
#include "platform/linux/LinuxPerformanceReport.h"
#include "platform/linux/LinuxScriptHttp.h"
#include "platform/linux/LinuxTarget.h"
#include "platform/linux/ble/BleScripting.h"
#include "platform/linux/ble/BleService.h"
#include "platform/linux/ble/GamepadApi.h"
#include "platform/linux/ble/GamepadScripting.h"
#include "platform/linux/images/HttpPictureLoader.h"
#include "platform/linux/images/RemoteImageStore.h"
#include "platform/linux/images/RemotePageIcon.h"
#include "platform/linux/iphone/IphoneBridge.h"
#include "platform/linux/iphone/LinuxCoverFetch.h"
#include "platform/linux/layout/LayoutPayload.h"
#include "platform/linux/layout/LayoutScripting.h"
#include "platform/linux/mqtt/KnobEntities.h"
#include "platform/linux/mqtt/KnobEvents.h"
#include "platform/linux/oauth/OAuthApi.h"
#include "platform/linux/oauth/OAuthScripting.h"
#include "platform/linux/oauth/OAuthService.h"
#include "platform/linux/oauth/OAuthTransport.h"
#include "platform/linux/oauth/OAuthVault.h"
#include "platform/linux/render/PageZoom.h"
#include "platform/linux/script/CryptoScripting.h"
#include "platform/linux/script/ExtensionHost.h"
#include "platform/linux/script/KnobScripting.h"
#include "platform/linux/script/TcpScripting.h"
#include "platform/linux/tls/BrokerTrust.h"
#include "platform/linux/tls/BrokerTrustApi.h"
#include "platform/tc002/audio/AudioScripting.h"
#include "platform/tc002/runtime/MicrophoneInput.h"
#include "platform/tc002/runtime/SupervisedRuntime.h"
#include "platform/tc002/runtime/SupervisorLink.h"
#include "platform/tc002/runtime/Tc002Board.h"
#include "platform/tc002/runtime/Tc002BootIntro.h"
#include "platform/tc002/runtime/Tc002Input.h"
#include "platform/tc002/runtime/Tc002KnobRouter.h"
#include "platform/tc002/runtime/Tc002Provisioning.h"
#include "platform/tc002/runtime/Tc002QuickSettings.h"
#include "platform/tc002/runtime/Tc002Speaker.h"
#include "platform/tc002/runtime/Tc002Target.h"
#include "platform/tc002/runtime/Tc002Update.h"
#include "platform/tc002/voice/KnobGesture.h"
#include "platform/tc002/voice/VoiceRuntime.h"
#include "system/MonotonicClock.h"
#include "transport/mqtt/MqttService.h"
#include "transport/net/DiscoveryService.h"
#include "transport/net/HostResolver.h"
#include "transport/net/MirrorLink.h"
#include "transport/ScriptMqttBridge.h"

namespace awtrix {

// awtrix-linux from start to exit: start() builds it, run() drives the frames until a signal, a
// reset or a failure stops it, stop() saves and releases it. Members are destroyed in reverse
// order, so each one is declared after everything it calls back into.
class LinuxRuntime {
 public:
  explicit LinuxRuntime(LinuxOptions options) : o_(std::move(options)) {}
  LinuxRuntime(const LinuxRuntime&) = delete;
  LinuxRuntime& operator=(const LinuxRuntime&) = delete;

  // -1 when the runtime is ready to run, otherwise the exit code.
  int start();
  void run();
  // The exit code.
  int stop();

 private:
  class System final : public ISystemService {
   public:
    void reboot() override { rebootRequested = true; stopping_ = 1; }
    void sleep(uint64_t) override { stopping_ = 1; }
    void factoryReset() override { resetAll = true; stopping_ = 1; }
    void resetSettings() override { resetUserSettings = true; stopping_ = 1; }
    bool resetAll = false, resetUserSettings = false, rebootRequested = false;
  };

  // The script extensions this runtime offers. They outlive their host adapter and the script VM.
  struct Extensions {
    std::unique_ptr<ble::BleScripting> ble;
    ble::RemoteGamepad phones{[] { return monotonicMs(); }};
    std::unique_ptr<ble::GamepadMux> gamepadInput;
    std::unique_ptr<ble::GamepadScripting> gamepad;
    std::unique_ptr<ble::GamepadApi> gamepadApi;
    linux_script::CryptoScripting crypto{[] { return monotonicMs(); }};
    linux_script::TcpScripting tcp;
    std::unique_ptr<layout::LayoutScripting> layout;
    std::unique_ptr<tc002::AudioScripting> audio;
    std::unique_ptr<script::KnobScripting> knob;
    std::vector<script::ScriptExtension*> list;
  };

  static void onSignal(int) { stopping_ = 1; }

  int selectTarget();
  int openChannels();
  int loadConfig();
  int composeEngine();
  void composeAccess();
  void composeConnectivity();
  void composeScriptServices();
  void composeBluetooth();
  void composeExtensions();
  void composePlatformRoutes();
  void startScripts();
  int startServices();
  void startBootIntro();
  void bindPlatformCommands();

  bool runFrame();
  bool pollSupervisor(int64_t now);
  bool pollInput(int64_t now);
  void tickVoice(int64_t now);
  void tickNetwork(int64_t now);
  bool reportReady();
  void saveDue(int64_t now);
  bool persist();
  bool eraseAll(std::error_code& error);

  void applyConfig();
  bool voiceAllowedNow() const;
  bool startVoice(int64_t now);
  float audioPitch(int64_t now);
  CoreEngine& engine() { return app_->engine(); }
  LinuxBoard& board() { return *selectedBoard_; }

  static inline volatile std::sig_atomic_t stopping_ = 0;

  LinuxOptions o_;
  Tc002Target tc002Target_;
  LinuxTargetPolicy headlessTarget_;
  std::unique_ptr<DefaultApps> headlessApps_;
  const LinuxTargetPolicy* target_ = nullptr;
  HostHttpOptions httpOptions_;
  std::unique_ptr<Tc002Update> update_;
  SupervisorLink link_;
  std::optional<LinuxPerformanceReport> performance_;
  int lock_ = -1;
  DeviceConfig cfg_;
  std::unique_ptr<LinuxMqttTls> mqttTls_;
  bool mqttAllowed_ = false;
  // The LAN service's MQTT over TLS trusts its broker as BrokerTrust decides.
  std::unique_ptr<tls::BrokerTrust> brokerTrust_;
  std::unique_ptr<tls::BrokerTrustApi> brokerTrustApi_;
  std::unique_ptr<LinuxBoard> selectedBoard_;
  Tc002Board* tc002_ = nullptr;
  Tc002Input input_;
  ScreenDisplay display_;
  System system_;
  SupervisedPageClock clock_;
  std::optional<images::HttpPictureLoader> pictureLoader_;
  std::optional<images::RemoteImageStore> remoteImages_;
  std::optional<RemotePageIcon> iconA_, iconB_;
  std::optional<PageZoom> pageZoom_;
  sound::AudioRouter audio_;
  AssetProbe assets_;
  std::optional<ApplicationServices> services_;
  layout::Limits layoutLimits_;
  std::shared_ptr<layout::Budget> layoutBudget_;
  MirrorLink mirror_;
  std::optional<ApplicationRuntime> app_;
  layout::Resources layoutResources_;
  std::optional<layout::LayoutPayload> layoutPayload_;
  std::unique_ptr<Tc002Speaker> speaker_;
  std::optional<SupervisedRuntime> supervision_;
  std::optional<Tc002QuickSettings> quickSettings_;
  std::optional<LinuxDeviceFacts> deviceFacts_;
  // UDP discovery answers only where the web server is reachable from the network.
  DiscoveryService discovery_;
  std::string discoveryName_;
  bool dirty_ = false;
  std::optional<Tc002Provisioning> provisioning_;
  LinuxLanLogin lanLogin_;
  std::string listenAddress_;
  std::optional<LinuxCapabilities> platformCapabilities_;
  HostHttpServer http_;
  MqttService mqtt_;
  std::unique_ptr<net::IHostResolver> resolver_;
  std::optional<PlatformDescriptor> platform_;
  ScriptStore<host::ScriptFiles> store_;
  ScriptIcon scriptIcon_;
  ScriptMqttBridge scriptMqtt_;
  script::ScriptServices scriptServices_;
  std::optional<tc002::MicrophoneInput> microphone_;
  tc002::voice::KnobGesture knobGesture_;
  std::unique_ptr<tc002::voice::VoiceRuntime> voice_;
  oauth::HttplibTransport oauthTransport_;
  std::optional<oauth::Vault> oauthVault_;
  std::optional<oauth::Service> oauthService_;
  std::optional<oauth::OAuthScripting> oauthScripting_;
  std::optional<oauth::Api> oauthApi_;
  std::optional<audio::AnalysisRouter> analysis_;
  Extensions extensions_;
  script::ScriptSlots scriptSlots_;
  script::ScriptHost* scripts_ = nullptr;
  std::unique_ptr<script::ExtensionHost> scriptExtensions_;
  // Stopped before its callback target is destroyed, also while an exception unwinds.
  LinuxScriptHttp scriptHttp_;
  // Declared after the scripts, so its worker stops before the host its events go to.
  std::unique_ptr<ble::BleService> bleService_;
  // Declared after the Bluetooth service it hands its requests to, and after the fetcher it asks.
  LinuxCoverFetch coverFetch_;
  std::unique_ptr<iphone::IphoneBridge> iphoneBridge_;
  HostButtonInput buttons_;
  std::optional<LinuxButtonWebhook> webhook_;
  std::optional<Tc002KnobRouter> knobRouter_;
  std::unique_ptr<BootSoundClock> bootSoundClock_;
  std::unique_ptr<Tc002BootIntro> intro_;
  ha::KnobEvents knobEvents_;
  std::vector<ha::Entity> mqttEntities_;
  SettingsSaver settingsSaver_;
  int64_t nextFrame_ = 0;
  bool displayFailed_ = false;
  bool inputFailed_ = false;
  bool readinessFailed_ = false;
  bool readySent_ = false;
  bool supervisorClosed_ = false;
};

}
