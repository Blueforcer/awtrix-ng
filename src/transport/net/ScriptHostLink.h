#pragma once

#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>

#include "core/remote/OfferList.h"

namespace awtrix {

class CoreEngine;
struct DeviceConfig;
namespace remote {
class HostActs;
class RemoteApps;
struct HostedScripts;
}
namespace script {
class IScriptHttp;
}

// Carries the script host link over UDP for a clock whose scripts run on a script host: looks the
// host up, feeds its datagrams to the remote client and hands the client what the clock knows -
// the apps on the display, the buttons, sensors and revisions. Script changes go on to the host's
// HTTP API. It also keeps the offers hosts send to the discovery port, paired or not, and follows a
// signed offer of its own host to a new address while the link is down.
class ScriptHostLink final {
 public:
  using Random = uint32_t (*)();
  using OwnAddress = uint32_t (*)(uint32_t host);

  ScriptHostLink();
  ~ScriptHostLink();
  ScriptHostLink(const ScriptHostLink&) = delete;
  ScriptHostLink& operator=(const ScriptHostLink&) = delete;

  // False when the address or the key in config cannot be used; the link then stays off.
  bool begin(remote::HostedScripts& hosted, const std::string& uid, int width, int height, uint16_t webPort,
             DeviceConfig& config, script::IScriptHttp& http, Random random, OwnAddress ownAddress);
  void tick(int64_t nowMs, bool online, CoreEngine& engine);
  void settingsChanged();
  void systemChanged();
  void assetsChanged();
  // A datagram that reached the discovery port; offers from hosts are kept, the rest ignored.
  void discovery(const uint8_t* data, std::size_t length, uint32_t fromAddress);

  bool running() const { return link_ != nullptr; }
  remote::HostActs* acts() const;
  const remote::RemoteApps* apps() const;
  std::string statusJson(const DeviceConfig& config, bool withKey) const;
  bool forward(const char* method, const std::string& path, std::string body);

 private:
  struct Link;

  std::unique_ptr<Link> link_;
  remote::OfferList offers_;
};

}
