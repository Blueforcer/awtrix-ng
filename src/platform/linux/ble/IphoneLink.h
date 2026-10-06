#pragma once

#include <cstdint>
#include <deque>
#include <functional>
#include <mutex>
#include <string>

#include "platform/linux/ble/Ams.h"
#include "platform/linux/ble/Ancs.h"
#include "platform/linux/ble/BleHub.h"

namespace awtrix::ble {

enum class IphoneState : uint8_t { Off, Waiting, Connecting, Ready };

// "off", "waiting", "connecting", "ready": the names the HTTP API uses.
const char* iphoneStateName(IphoneState state);

struct IphoneStatus {
  IphoneState state = IphoneState::Off;
  // What the linked phone offers: its notifications (ANCS) and its player (AMS). False unless Ready.
  bool notifications = false;
  bool music = false;
};

// What the link reports to the main loop, in the order it happened.
struct IphoneEvent {
  enum class Kind : uint8_t { Phone, Notification, Music };
  Kind kind = Kind::Notification;
  // Phone: the phone to remember from now on, and its name.
  Address phone;
  std::string name;
  ancs::Attributes notification;
  // Music: the player at the moment the event was made, its elapsedAtMs meaningless across the
  // thread boundary; nothing plays once the phone is gone.
  ams::Playback music;
};

struct IphoneConfig {
  bool enabled = false;
  bool havePhone = false;
  Address phone;
};

// The main loop's side of the link, which the Bluetooth worker carries out.
class IphoneControl {
 public:
  virtual ~IphoneControl() = default;
  virtual IphoneStatus iphoneStatus() const = 0;
  virtual std::deque<IphoneEvent> takeIphoneEvents() = 0;
  virtual void configureIphone(const IphoneConfig& config) = 0;
  // Removes the phone's bond and drops its link.
  virtual void forgetIphone() = 0;
};

// The link to an iPhone, kept without any script: the display advertises that it would use the
// phone's notification service, the phone pairs and connects, and the display, as a client on the
// phone's own link, subscribes to its notifications and its player. The first phone that gets
// that far is remembered, and from then on only that phone is taken. It uses the hub under an
// owner name no script can have, like the gamepad. Worker thread only, apart from status() and
// take().
class IphoneLink {
 public:
  static constexpr const char* kOwner = "@iphone";
  static constexpr int64_t kRetryMs = 2000;
  static constexpr int64_t kRetryMaxMs = 60000;
  static constexpr int64_t kRequestTimeoutMs = 3000;
  static constexpr std::size_t kPendingMax = 16;
  static constexpr std::size_t kEventsMax = 64;

  IphoneLink(BleHub& hub, std::string name, std::function<void(const std::string&)> log);

  void configure(const IphoneConfig& config, int64_t nowMs);
  void forget(int64_t nowMs);
  // Events the hub sent to kOwner. They wait for tick(): the hub is mid-operation when it sends.
  void onEvent(BleEvent event);
  void tick(int64_t nowMs);

  IphoneStatus status() const;
  std::deque<IphoneEvent> take();

 private:
  void stopAll();
  void advertise();
  void advertFailed(const std::string& why);
  void link(const Address& central);
  void setUp();
  void ready();
  void lost(const std::string& why);
  void release();
  void handle(const BleEvent& e);
  void onCentral(const BleEvent& e);
  void onNotice(const Bytes& value);
  void onData(const Bytes& value);
  void onUpdate(const Bytes& value);
  void remember(const std::string& name);
  void request();
  void finishRequest();
  void publish(IphoneState state);
  void emit(IphoneEvent event);
  void clearMusic();
  void emitMusic();
  std::string call(const char* op, const std::string& args, uint32_t id);
  std::string target(const char* service, const char* characteristic, const Bytes& data = {}) const;
  uint32_t next() { return ++lastId_; }

  BleHub& hub_;
  std::string name_;
  std::function<void(const std::string&)> log_;
  std::deque<BleEvent> events_;
  int64_t now_ = 0;

  bool enabled_ = false;
  bool haveKnown_ = false;
  Address known_;
  Address central_;
  bool hasMusic_ = false;
  IphoneState state_ = IphoneState::Off;
  int64_t advertiseAt_ = -1;
  std::string advertError_;
  int64_t relinkAt_ = -1;
  int failures_ = 0;
  uint32_t lastId_ = 0;
  uint32_t advertId_ = 0, connId_ = 0, dataId_ = 0, noticeId_ = 0, updateId_ = 0;
  uint32_t trackSetupId_ = 0, playerSetupId_ = 0, nameId_ = 0, requestId_ = 0;

  std::deque<uint32_t> pending_;
  ancs::Assembler assembler_;
  int64_t requestUntil_ = -1;
  ams::Playback playback_;

  mutable std::mutex mutex_;
  IphoneStatus status_;
  std::deque<IphoneEvent> out_;
};

}
