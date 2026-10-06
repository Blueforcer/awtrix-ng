#pragma once

#include <deque>
#include <functional>
#include <map>
#include <string>
#include <vector>

#include "platform/linux/ble/Att.h"
#include "platform/linux/ble/BleHub.h"
#include "platform/linux/ble/GattServer.h"

namespace awtrix::ble::testing {

inline Uuid uuid(const char* s) {
  Uuid u;
  Uuid::parse(s, u);
  return u;
}

inline Address mkaddr(const char* s, bool random = false) {
  Address a;
  Address::parse(s, random, a);
  return a;
}

// A controller whose answers arrive later, on pump(), like the real one's do. Outgoing links
// reach a peer made of a GattServer; the test drives incoming links and adverts itself.
class FakeRadio : public BleRadio {
 public:
  void setEvents(Events e) override { events = std::move(e); }
  void power(bool on) override {
    powerCalls.push_back(on);
    later([this, on] { events.power(on ? Power::On : Power::Off, ""); });
  }
  Address address() const override { return mkaddr("02:00:00:00:00:01"); }
  void setName(const std::string&) override {}
  bool scan(bool on, bool active, bool background) override {
    scanning = on;
    scanActive = active;
    scanBackground = background;
    ++scanCalls;
    return true;
  }
  int connect(const Address& peer, int level) override {
    ++connectCalls;
    const int id = nextLink++;
    peers[id] = peer;
    levels[id] = 1;
    wantedLevel[id] = level;
    later([this, id] {
      if (failNextConnect) {
        failNextConnect = false;
        events.connected(id, false, "no answer");
        return;
      }
      peerDb.linkUp(id);
      events.connected(id, true, "");
    });
    return id;
  }
  void disconnect(int link) override {
    disconnected.push_back(link);
    peerDb.linkDown(link);
    later([this, link] { events.disconnected(link); });
  }
  bool send(int link, const Bytes& pdu) override {
    sent.emplace_back(link, pdu);
    if (!peers.count(link) || silent) return true;
    later([this, link, pdu] {
      if (!pdu.empty() && pdu[0] == att::kWriteReq && le16(pdu.data() + 1) == lockedHandle && levels[link] < 2) {
        const Bytes e = att::error(pdu[0], lockedHandle, att::kInsufficientAuthentication);
        events.att(link, e.data(), e.size());
        return;
      }
      if (!att::toServer(pdu[0])) return;
      const Bytes rsp = peerDb.handle(link, pdu.data(), pdu.size(), levels[link]);
      if (!rsp.empty()) events.att(link, rsp.data(), rsp.size());
      for (auto& [l, p] : peerDb.drain(level())) events.att(l, p.data(), p.size());
    });
    return true;
  }
  bool secure(int link, int level) override {
    later([this, link, level] {
      levels[link] = level;
      events.secured(link, true, "");
    });
    return true;
  }
  int security(int link) const override {
    auto it = levels.find(link);
    return it == levels.end() ? 1 : it->second;
  }
  Address peer(int link) const override {
    auto it = peers.find(link);
    return it == peers.end() ? Address() : it->second;
  }
  int advertisingSlots() const override { return 2; }
  bool advertise(int instance, bool, const Bytes& adv, const Bytes&, std::string&) override {
    adverts[instance] = adv;
    return true;
  }
  void stopAdvertising(int instance) override { adverts.erase(instance); }
  std::vector<Address> bonds() const override { return {}; }
  bool forget(const Address& peer) override {
    forgotten.push_back(peer.str());
    return true;
  }

  void later(std::function<void()> f) { queue.push_back(std::move(f)); }
  // The peer sends a value that needs an encrypted link only over one.
  std::function<int(int)> level() {
    return [this](int link) { return levels.count(link) ? levels[link] : 1; };
  }
  void pump(BleHub& hub, int64_t& now) {
    for (int guard = 0; guard < 1000 && !queue.empty(); ++guard) {
      auto f = std::move(queue.front());
      queue.pop_front();
      f();
      hub.tick(now);
    }
  }
  void advert(const char* addr, uint8_t type, int8_t rssi, Bytes data) {
    AdvReport r;
    r.addr = mkaddr(addr);
    r.eventType = type;
    r.rssi = rssi;
    r.data = std::move(data);
    events.advert(r);
  }
  void notifyPeer(uint16_t handle, Bytes value) {
    peerDb.setValue(handle, std::move(value));
    peerDb.publish(handle);
    for (auto& [l, p] : peerDb.drain(level())) later([this, l = l, p = p] { events.att(l, p.data(), p.size()); });
  }

  Events events;
  GattServer peerDb;
  std::deque<std::function<void()>> queue;
  std::vector<bool> powerCalls;
  std::vector<std::pair<int, Bytes>> sent;
  std::vector<int> disconnected;
  std::map<int, Address> peers;
  std::map<int, int> levels;
  std::map<int, int> wantedLevel;
  std::map<int, Bytes> adverts;
  bool scanning = false;
  bool scanActive = false;
  bool scanBackground = false;
  std::vector<std::string> forgotten;
  bool failNextConnect = false;
  bool silent = false;
  int scanCalls = 0;
  int connectCalls = 0;
  int nextLink = 100;
  uint16_t lockedHandle = 0;
};

}
