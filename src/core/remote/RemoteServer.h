#pragma once

#include <cstddef>
#include <cstdint>
#include <functional>
#include <map>
#include <string>
#include <vector>

#include "core/net/Datagram.h"
#include "core/remote/RemoteWire.h"
#include "core/render/Canvas.h"

namespace awtrix {
namespace remote {

struct HostApp {
  std::string name;
  uint8_t flags = 0;
  uint32_t durationMs = 0;
};

// What the clock last told about itself in a valid Hello.
struct ClockState {
  uint32_t counter = 0;
  uint16_t width = 0;
  uint16_t height = 0;
  uint32_t address = 0;
  uint16_t webPort = 0;
  uint8_t state = 0;
  wire::Sensors sensors;
  uint16_t settingsRevision = 0;
  uint16_t systemRevision = 0;
  uint16_t assetsRevision = 0;
};

// The script host's end of the link for one clock. It takes Hello only with the clock's uid, a
// valid tag and, while connected, a counter above the last one, and then talks to wherever that
// Hello came from. It keeps the clock's app list current once there is one, tells which apps the
// clock wants drawn and which it switched off, replays its buttons frame by frame, and sends a
// picture of an app only when it changed, or once a second.
class RemoteServer {
 public:
  static constexpr int64_t kAppsEveryMs = 2000;
  static constexpr int64_t kKeepaliveMs = 1000;
  static constexpr int64_t kShowSilenceMs = 1500;
  static constexpr int64_t kClockSilenceMs = 6000;

  using Verify = std::function<bool(const wire::Packet& hello)>;

  RemoteServer(net::IDatagramSink& sink, std::string uid, Verify verify, uint16_t appsRevision)
      : sink_(sink), uid_(std::move(uid)), verify_(std::move(verify)), appsRevision_(appsRevision) {}

  void receive(const net::Endpoint& from, const uint8_t* data, std::size_t length, int64_t nowMs);
  void setApps(std::vector<HostApp> apps, int64_t nowMs);
  void tick(int64_t nowMs);

  bool connected() const { return connected_; }
  const ClockState& clock() const { return clock_; }
  const net::Endpoint& endpoint() const { return endpoint_; }
  uint32_t connection() const { return connection_; }
  const std::vector<std::string>& switchedOff() const { return switchedOff_; }
  // Whether switchedOff() is what the clock has: true once a Hello of this connection carried the
  // current apps revision, false again when the list or the connection changes.
  bool switchesKnown() const { return switchesKnown_; }
  // The apps the clock wants drawn; empty once it stopped asking.
  const std::string& current() const { return current_; }
  const std::string& incoming() const { return incoming_; }

  // Once per frame: whether each button counts as pressed and held for the scripts. A press the
  // clock counted is reported for at least one frame, and two presses with a frame up between.
  void stepButtons(bool (&pressed)[wire::kButtons]);

  void sendFrame(const std::string& app, const Canvas& canvas, int64_t nowMs);

 private:
  struct Sent {
    uint32_t fingerprint = 0;
    int64_t atMs = 0;
  };
  struct Button {
    unsigned pending = 0;
    bool inPress = false;
  };

  void hello(const wire::Packet& packet, const net::Endpoint& from, int64_t nowMs);
  void show(const wire::Show& show, int64_t nowMs);
  void forgetShow();
  void sendApps(int64_t nowMs);
  void takeOffMask(uint32_t mask);

  net::IDatagramSink& sink_;
  std::string uid_;
  Verify verify_;
  net::Endpoint endpoint_;
  bool connected_ = false;
  uint32_t connection_ = 0;
  uint32_t counter_ = 0;
  int64_t lastHelloMs_ = 0;
  ClockState clock_;

  std::vector<HostApp> apps_;
  bool listed_ = false;
  uint16_t appsRevision_ = 0;
  int64_t lastAppsMs_ = 0;
  std::vector<std::string> switchedOff_;
  bool switchesKnown_ = false;

  bool haveShow_ = false;
  uint16_t sequence_ = 0;
  int64_t lastShowMs_ = 0;
  std::string current_;
  std::string incoming_;
  uint8_t down_ = 0;
  uint8_t presses_[wire::kButtons] = {0, 0, 0};
  Button buttons_[wire::kButtons];

  std::map<std::string, Sent> sent_;
};

}
}
