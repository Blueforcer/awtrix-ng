#pragma once

#include <cstddef>
#include <cstdint>
#include <string>

#include "core/net/Datagram.h"
#include "core/remote/RemoteApps.h"
#include "core/remote/RemoteFrames.h"
#include "core/remote/RemoteWire.h"

namespace awtrix {
namespace remote {

// The clock's end of the script host link. It greets the host every two seconds with its size, the
// address and port its web server answers at, state, sensors and revisions, and at once when the
// host's app list was applied or an app was switched on or off; it tells the host which host apps
// the display needs and which buttons are down, and takes the app list and the pictures back. Only
// datagrams from the host's address count; the link is up while the host was heard within the last
// three seconds. A signed offer from the host moves the link to wherever the host is now.
class RemoteClient {
 public:
  static constexpr int64_t kHelloEveryMs = 2000;
  static constexpr int64_t kShowEveryMs = 250;
  static constexpr int64_t kSilenceMs = 3000;
  static constexpr int64_t kKnownUnixTime = 1600000000;
  static constexpr int64_t kOfferSkewS = 120;

  using Random = uint32_t (*)();
  using OwnAddress = uint32_t (*)(uint32_t host);

  RemoteClient(net::IDatagramSink& sink, RemoteFrames& frames, RemoteApps& apps)
      : sink_(sink), frames_(frames), apps_(apps) {}

  void begin(std::string uid, int width, int height, uint16_t webPort, OwnAddress ownAddress, wire::Signer sign,
             Random random);
  void setHost(const net::Endpoint& host);
  bool hasHost() const { return host_.address != 0 && host_.port != 0; }

  void setState(uint8_t state, const wire::Sensors& sensors);
  void settingsChanged() { ++settingsRevision_; }
  void systemChanged() { ++systemRevision_; }
  void assetsChanged() { ++assetsRevision_; }
  // down: bit per button (left, select, right); presses: how often each went down so far.
  void setButtons(uint8_t down, const uint8_t (&presses)[wire::kButtons]);

  // current and incoming are the engine's app ids; only host apps among them are asked for.
  void tick(const std::string& current, const std::string& incoming, int64_t nowMs);
  void receive(const net::Endpoint& from, const uint8_t* data, std::size_t length, int64_t nowMs);
  void dropLink() { setLink(false); }
  bool follow(const wire::Packet& offer, uint32_t fromAddress, int64_t unixNow);

  bool linkUp() const { return linkUp_; }

 private:
  void sendHello();
  void sendShow();
  void setLink(bool up);

  net::IDatagramSink& sink_;
  RemoteFrames& frames_;
  RemoteApps& apps_;
  std::string uid_;
  uint16_t width_ = 0;
  uint16_t height_ = 0;
  uint16_t webPort_ = 0;
  OwnAddress ownAddress_ = nullptr;
  wire::Signer sign_;
  net::Endpoint host_;

  uint8_t state_ = 0;
  wire::Sensors sensors_;
  uint32_t helloCounter_ = 0;
  uint16_t settingsRevision_ = 0;
  uint16_t systemRevision_ = 0;
  uint16_t assetsRevision_ = 0;
  uint16_t greetedAppsRevision_ = 0;
  uint32_t greetedOffMask_ = 0;
  bool greeted_ = false;
  int64_t lastHelloMs_ = 0;
  uint32_t offerTime_ = 0;

  std::string current_;
  std::string incoming_;
  uint8_t down_ = 0;
  uint8_t presses_[wire::kButtons] = {0, 0, 0};
  bool showDirty_ = false;
  uint16_t sequence_ = 0;
  int64_t lastShowMs_ = 0;

  bool linkUp_ = false;
  int64_t lastHeardMs_ = 0;
};

}
}
