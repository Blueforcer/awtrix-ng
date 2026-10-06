#pragma once

#include <cstdint>
#include <deque>
#include <functional>
#include <map>
#include <string>
#include <utility>
#include <vector>

#include "platform/linux/ble/BleTypes.h"

namespace awtrix::ble {

// The attribute database every script's services share, and the server half of every link.
// Values live here, so a peer reads without waiting for a script; writes and subscriptions are
// reported so the owning script hears of them.
class GattServer {
 public:
  struct CharacteristicSpec {
    Uuid uuid;
    uint8_t props = 0;
    bool encrypted = false;
    Bytes value;
    // Read-only descriptors after the configuration descriptor, such as a Report Reference.
    std::vector<std::pair<Uuid, Bytes>> descriptors;
  };

  GattServer();

  void setDeviceName(const std::string& name);

  // Service ids start at 1. valueHandles gets one handle per characteristic, in order.
  int addService(const Uuid& uuid, const std::vector<CharacteristicSpec>& characteristics,
                 std::vector<uint16_t>& valueHandles);
  void removeService(int id);
  bool hasService(const Uuid& uuid) const;
  bool setValue(uint16_t valueHandle, Bytes value);

  void linkUp(int link);
  void linkDown(int link);
  uint16_t mtu(int link) const;

  // The answer to one request from the peer; empty for commands and confirmations.
  Bytes handle(int link, const uint8_t* pdu, std::size_t n, int securityLevel);

  // Queues what a changed value owes each subscribed link: notifications go out at once,
  // indications one at a time per link, each after the previous one was confirmed.
  void publish(uint16_t valueHandle);
  // Everything that may be sent now. Call after publish(), after a service came or went and
  // after handle(), which is where confirmations arrive.
  std::vector<std::pair<int, Bytes>> drain(const std::function<int(int)>& security = {});

  std::function<void(int link, uint16_t valueHandle, const Bytes& value)> onWrite;
  std::function<void(int link, uint16_t valueHandle, uint16_t config)> onSubscribe;

 private:
  enum class Kind { Service, Declaration, Value, ClientConfig, Descriptor };
  struct Attribute {
    uint16_t handle = 0;
    Uuid type;
    Kind kind = Kind::Value;
    Bytes value;
    uint8_t props = 0;
    bool encrypted = false;
    uint16_t configures = 0;
    int service = 0;
  };
  struct Link {
    uint16_t mtu = 23;
    std::map<uint16_t, uint16_t> config;
    std::deque<Bytes> notifications;
    std::deque<Bytes> indications;
    bool awaitingConfirm = false;
  };

  Attribute* find(uint16_t handle);
  uint16_t groupEnd(std::size_t index) const;
  bool readable(const Attribute& a, int securityLevel, uint8_t& error) const;
  Bytes readValue(int link, const Attribute& a) const;
  void announce(uint16_t start, uint16_t end);

  std::vector<Attribute> attributes_;
  std::map<int, Link> links_;
  uint16_t nextHandle_ = 0;
  int nextService_ = 1;
};

}
