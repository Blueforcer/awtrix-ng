#pragma once

#include <poll.h>

#include <cstdint>
#include <deque>
#include <functional>
#include <map>
#include <optional>
#include <string>
#include <vector>

#include "platform/linux/ble/BleRadio.h"
#include "platform/linux/ble/BondStore.h"
#include "platform/linux/ble/MgmtSocket.h"

namespace awtrix::ble {

// The controller through the kernel's own interfaces, without a Bluetooth daemon: the management
// socket for power, pairing, keys and adverts, a raw HCI socket for scanning, and L2CAP sockets on
// the ATT channel for every link. Everything is non-blocking except management commands, which
// answer within milliseconds and are awaited in place.
class LinuxBleRadio : public BleRadio {
 public:
  struct Options {
    // Where pairing keys survive the controller being switched off; empty keeps them in memory.
    std::string bondsPath;
    // Asks the supervisor to attach (true) or release (false) the controller; null when the
    // controller is always there, as on a desktop.
    std::function<void(bool)> requestController;
    int64_t attachTimeoutMs = 10000;
    int connectAttempts = 3;
    int64_t retryDelayMs = 1000;
    int64_t secureTimeoutMs = 30000;
    std::function<void(const std::string&)> log;
  };

  explicit LinuxBleRadio(Options options);
  ~LinuxBleRadio() override;

  void setEvents(Events events) override { events_ = std::move(events); }
  void power(bool on) override;
  Address address() const override { return address_; }
  void setName(const std::string& name) override;
  bool scan(bool on, bool active, bool background) override;
  int connect(const Address& peer, int securityLevel) override;
  void disconnect(int link) override;
  bool send(int link, const Bytes& pdu) override;
  bool secure(int link, int securityLevel) override;
  int security(int link) const override;
  Address peer(int link) const override;
  int advertisingSlots() const override { return slots_; }
  bool advertise(int instance, bool connectable, const Bytes& adv, const Bytes& scanResponse,
                 std::string& error) override;
  void stopAdvertising(int instance) override;
  std::vector<Address> bonds() const override;
  bool forget(const Address& peer) override;

  // The supervisor's answer to requestController. Off without a reason is the controller being
  // released, which a new runtime can still hear from the one before it, never a refusal.
  void controllerStatus(bool on, const std::string& error);

  void collect(std::vector<pollfd>& out) const;
  void dispatch(const std::vector<pollfd>& ready, int64_t nowMs);
  void tick(int64_t nowMs);

 private:
  enum class State { Off, Waiting, On };
  struct Link {
    int fd = -1;
    Address peer;
    bool outgoing = false;
    bool peripheral = false;
    bool direct = false;
    bool open = false;
    int level = 1;
    int attempts = 0;
    int64_t retryAt = -1;
    int64_t connectDeadline = -1;
    std::optional<uint16_t> handle;
    std::deque<Bytes> out;
    int secureTarget = 0;
    int64_t secureDeadline = -1;
  };
  // A raw initiation being undone: cancelled while pending, or disconnected once the controller
  // made the link. Until it ends no other connection starts, and the ATT socket it may still
  // produce is closed.
  struct DirectCancel {
    Address peer;
    std::optional<uint16_t> handle;
    bool discardSocket = true;
    bool resent = false;
    int64_t deadline = -1;
  };
  struct Advert {
    bool connectable = false;
    bool scannable = false;
  };

  int mgmt(uint16_t op, const Bytes& params, Bytes* reply = nullptr);
  void mgmtEvent(const uint8_t* p, std::size_t n);
  void readMgmt();
  void configure();
  void fail(const std::string& why);
  void shutdown();
  bool openScanner();
  void readScanner();
  bool hciCommand(uint16_t op, const Bytes& params);
  void hciEvent(const uint8_t* p, std::size_t n);
  void applyScan();
  void restoreAdvertising();
  void resume();
  bool connecting() const;
  bool incoming() const;
  bool openListener();
  void acceptLink();
  int nextQueuedConnection() const;
  void dequeueConnection(int id);
  bool startConnect(int id, Link& link);
  void failDirect(const std::string& why);
  void cancelDirect(const Link& link);
  bool sendDisconnect(uint16_t handle);
  void finishCancel();
  void finishConnect(int id, Link& link);
  void readLink(int id, Link& link);
  void flush(Link& link);
  void close(int id, bool notify);
  void note(const std::string& text) const;

  Options options_;
  BondStore bonds_;
  MgmtSocket management_;
  Events events_;
  State state_ = State::Off;
  int scanFd_ = -1;
  int listenFd_ = -1;
  uint16_t index_ = 0;
  Address address_;
  std::string name_ = "AWTRIX";
  int slots_ = 0;
  int64_t now_ = 0;
  int64_t waitDeadline_ = -1;
  bool scanWanted_ = false;
  bool scanActive_ = false;
  bool scanBackground_ = false;
  int64_t lastReport_ = 0;
  std::map<int, Advert> adverts_;
  std::map<int, Link> links_;
  // Pending attempts in arrival order. A retry's backoff is kept in Link::retryAt.
  std::deque<int> connectQueue_;
  int nextLink_ = 1;
  bool legacyKernel_ = false;
  int directLink_ = 0;
  std::optional<DirectCancel> cancel_;
  std::map<uint16_t, uint8_t> hciErrors_;
  std::map<uint16_t, bool> peripheralHandles_;
};

}
