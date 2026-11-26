#pragma once

#include <cstdint>
#include <deque>
#include <functional>
#include <memory>
#include <optional>
#include <set>
#include <string>
#include <vector>

#include "core/apps/IApp.h"
#include "core/net/Datagram.h"
#include "core/remote/RemoteServer.h"
#include "core/render/Canvas.h"
#include "core/sound/PcmPlayback.h"
#include "media/ScriptIcon.h"
#include "platform/linux/remote/ClockApi.h"
#include "transport/net/UdpPort.h"

namespace awtrix {
class ApplicationRuntime;
class CoreEngine;
struct ApplicationServices;
namespace script {
class ScriptHost;
}
}

namespace awtrix::remote {

struct RoleOptions {
  std::string uid;
  std::string keyFile;
  std::string clockUrl;
};

// awtrix-linux as the script host of one ESP32 clock: the scripts run here, ticked for the apps the
// clock asks for, and their pictures go to the clock over UDP on the same port number as the web
// server. The clock's buttons, readings, switches, settings and time zone are taken over; what a
// script does to the clock goes to its HTTP API at the address and port its last valid Hello names,
// or at --remote-clock-url while the Hello names none.
class RemoteRole final : private IIconMissing {
 public:
  using OnTimezone = std::function<void(const std::string& tz, const std::string& tzName)>;

  // Reads the key file and opens the port; false with the reason otherwise.
  bool begin(const RoleOptions& options, uint16_t port, int width, int height, std::string& error);
  void setOnTimezone(OnTimezone onTimezone) { onTimezone_ = std::move(onTimezone); }
  // The clock's MQTT broker, for the scripts here: called when it is first known and when it changes.
  struct Broker {
    bool enabled = false;
    std::string host, user, pass, prefix;
    long port = 1883;
    bool operator==(const Broker& o) const {
      return enabled == o.enabled && host == o.host && user == o.user && pass == o.pass && prefix == o.prefix &&
             port == o.port;
    }
  };
  using OnBroker = std::function<void(const Broker&)>;
  void setOnBroker(OnBroker onBroker) { onBroker_ = std::move(onBroker); }

  const std::string& keyHex() const { return keyHex_; }
  ClockApi& clock() { return *clock_; }
  // Icons a script names but this host lacks are fetched from the clock's /ICONS, and fetched again
  // whenever the clock's files change.
  void bindIcons(ScriptIcon& icons);
  // Before the scripts are restored: no app the clock may have switched off starts before the
  // clock told which ones it switched off.
  void bindScripts(script::ScriptHost& scripts, CoreEngine& engine);

  void act(std::string body, std::function<void()> answered = {});
  // Asks the clock to hold or release the rotation when it does not already, as far as its last
  // Hello after the previous ask tells.
  void hold(bool on);
  const std::string& shown() const { return server_->current(); }
  bool audioPlaying() const { return (server_->clock().state & wire::state::kAudioPlaying) != 0; }
  const sound::Caps* soundCaps() const { return soundCaps_ ? &*soundCaps_ : nullptr; }

  // Runs in place of the scripts' part of ApplicationRuntime::tick.
  void tick(int64_t nowMs, ApplicationRuntime& app, const ApplicationServices& services,
            script::ScriptHost* scripts);

 private:
  static constexpr int64_t kOfferEveryMs = 500;
  static constexpr int64_t kRetryMs = 5000;
  static constexpr int64_t kSessionLingerMs = 5000;

  struct Copy {
    uint64_t applied = UINT64_MAX;
    bool asking = false;
    int64_t retryAtMs = 0;
  };
  struct WantedIcon {
    std::string name;
    int64_t retryAtMs = 0;
  };

  void iconMissing(const std::string& name) override;
  bool clockHolds() const;
  void followClock();
  void takeReadings(ApplicationRuntime& app);
  void sync(int64_t nowMs, CoreEngine& engine);
  static bool due(const Copy& copy, uint64_t wanted, int64_t nowMs);
  void fetch(Copy& copy, uint64_t wanted, const char* path, std::function<void(const std::string& body)> apply);
  void applySystem(const std::string& system);
  void queueIcon(const std::string& name);
  void nextIcon();
  void fetchIcon(const std::string& name, const char* extension);
  void iconDone(const std::string& name, const char* extension, int status, const std::string& body);
  void offerApps(int64_t nowMs, script::ScriptHost& scripts);
  void followSwitches(CoreEngine& engine, script::ScriptHost& scripts);
  void followSession(int64_t nowMs, CoreEngine& engine, script::ScriptHost& scripts);
  void pressButtons(script::ScriptHost& scripts);
  void draw(ApplicationRuntime& app, const std::string& name, Canvas& canvas, const RenderCtx& ctx, int64_t nowMs);

  std::string keyHex_;
  std::string clockHost_;
  uint16_t clockPort_ = 80;
  net::Endpoint clockAt_;
  std::unique_ptr<ClockApi> clock_;
  net::UdpPort port_;
  std::unique_ptr<RemoteServer> server_;
  std::unique_ptr<Canvas> current_;
  std::unique_ptr<Canvas> incoming_;
  OnTimezone onTimezone_;
  OnBroker onBroker_;
  Broker broker_;
  bool haveBroker_ = false;
  int64_t nowMs_ = 0;

  Copy caps_, settings_, system_;
  uint64_t assets_ = UINT64_MAX;
  std::optional<sound::Caps> soundCaps_;

  ScriptIcon* icons_ = nullptr;
  std::set<std::string> iconNames_;
  std::deque<WantedIcon> iconQueue_;
  bool iconAsking_ = false;
  bool iconsChanged_ = false;

  int64_t appsAtMs_ = 0;
  std::vector<std::string> offered_;
  std::vector<std::string> appliedOff_;
  bool switchesApplied_ = false;
  int64_t sessionAskedMs_ = 0;
  int64_t sessionRetryAtMs_ = 0;

  bool down_[wire::kButtons] = {};
  bool holdAsked_ = false;
  bool holdOut_ = false;
  uint32_t holdAnsweredAt_ = 0;
  int lastActStatus_ = 200;
};

}
