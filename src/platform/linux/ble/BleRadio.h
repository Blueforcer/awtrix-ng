#pragma once

#include <cstdint>
#include <functional>
#include <string>
#include <vector>

#include "platform/linux/ble/BleTypes.h"

namespace awtrix::ble {

enum class Power { Off, Starting, On, Failed };

struct AdvReport {
  Address addr;
  // 0 connectable, 1 directed, 2 scannable, 3 not connectable, 4 scan response.
  uint8_t eventType = 0;
  int8_t rssi = 0;
  Bytes data;
};

// The controller as the hub sees it. Every call returns at once; results arrive as events on
// the same thread. Link ids are the radio's own and never reused within a power cycle.
class BleRadio {
 public:
  struct Events {
    std::function<void(Power, const std::string& error)> power;
    std::function<void(const AdvReport&)> advert;
    std::function<void(int link, bool ok, const std::string& error)> connected;
    std::function<void(int link, const Address& peer)> accepted;
    std::function<void(int link)> disconnected;
    std::function<void(int link, const uint8_t* pdu, std::size_t n)> att;
    std::function<void(int link, bool ok, const std::string& error)> secured;
  };

  virtual ~BleRadio() = default;
  virtual void setEvents(Events events) = 0;

  virtual void power(bool on) = 0;
  virtual Address address() const = 0;
  virtual void setName(const std::string& name) = 0;

  // background: nothing is in a hurry, so the radio listens a small share of the time.
  virtual bool scan(bool on, bool active, bool background) = 0;

  virtual int connect(const Address& peer, int securityLevel) = 0;
  virtual void disconnect(int link) = 0;
  virtual bool send(int link, const Bytes& pdu) = 0;
  virtual bool secure(int link, int securityLevel) = 0;
  virtual int security(int link) const = 0;
  virtual Address peer(int link) const = 0;

  virtual int advertisingSlots() const = 0;
  virtual bool advertise(int instance, bool connectable, const Bytes& adv, const Bytes& scanResponse,
                         std::string& error) = 0;
  virtual void stopAdvertising(int instance) = 0;

  virtual std::vector<Address> bonds() const = 0;
  virtual bool forget(const Address& peer) = 0;
};

}
