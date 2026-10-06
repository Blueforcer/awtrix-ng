#pragma once

#include <cstdint>
#include <functional>
#include <map>
#include <memory>
#include <set>
#include <string>
#include <vector>

#include "platform/linux/ble/BleScanner.h"
#include "platform/linux/ble/GattClient.h"
#include "platform/linux/ble/GattServer.h"

namespace awtrix::ble {

// Shares one controller between every script. A resource a script holds - a scan, a
// connection, a subscription, an advert, a service - is keyed by the script and the callback
// id it registered, so each script sees only its own share and forget() removes exactly that.
// Physical things are shared: one scan with every script's filters applied to each report,
// one link per peer however many scripts use it, one attribute database for all services.
// Single-threaded: call, forget, tick and the radio's events all run on one thread.
class BleHub {
 public:
  using Sink = BleScanner::Sink;

  struct Limits {
    std::size_t scansPerScript = 4;
    std::size_t linksPerScript = 2;
    std::size_t subscriptionsPerScript = 8;
    std::size_t advertsPerScript = 1;
    std::size_t servicesPerScript = 4;
    std::size_t links = 4;
    int advertEventsPerSecond = 20;
  };
  static constexpr int64_t kIdleOffMs = 30000;
  static constexpr int64_t kPowerRetryMs = 5000;

  BleHub(BleRadio& radio, Sink sink);
  BleHub(BleRadio& radio, Sink sink, Limits limits);

  std::string call(const std::string& script, const std::string& op, const std::string& args, uint32_t id,
                   int64_t nowMs);
  void forget(const std::string& script, int64_t nowMs);
  void tick(int64_t nowMs);
  Power power() const { return power_; }
  void setDeviceName(const std::string& name) { server_.setDeviceName(name); }
  std::function<void(const std::string&)> log;
  bool idle() const;
  // Whether any link, whoever opened it, goes to this peer.
  bool linked(const Address& peer) const;
  // Closes every link to this peer, whoever opened it, a central's own included. For the
  // firmware's own owners: a script lets go of a link, it does not close another's.
  void disconnect(const Address& peer);

 private:
  using Key = ResourceKey;
  enum Resource { kLinks, kSubscriptions, kAdverts, kServices };
  struct Link {
    Address peer;
    bool outgoing = false;
    bool ready = false;
    std::unique_ptr<GattClient> client;
    std::set<Key> waiting;
    std::set<Key> users;
    std::map<uint16_t, std::set<Key>> subscribers;
    std::vector<Key> pairing;
    std::vector<std::function<void(bool)>> afterSecure;
  };
  // intervalMs > 0 thins a fast stream: at most one value per interval reaches the script, always
  // the newest, and the last one of a burst follows once its interval is over.
  struct Subscription {
    int link = 0;
    std::vector<uint16_t> handles;
    int64_t intervalMs = 0;
    int64_t lastSent = -1;
    std::string pending;
  };
  struct Served {
    int service = 0;
    std::map<uint16_t, Uuid> byHandle;
  };
  struct Pending {
    Key key;
    std::string op;
    std::string args;
  };
  std::string execute(const Key& key, const std::string& op, const std::string& args);
  std::string startScan(const Key& key, const std::string& args);
  std::string connect(const Key& key, const std::string& args);
  std::string gattOp(const Key& key, const std::string& op, const std::string& args);
  std::string subscribe(const Key& key, const std::string& args);
  std::string pair(const Key& key, const std::string& args);
  std::string advertise(const Key& key, const std::string& args);
  std::string serve(const Key& key, const std::string& args);
  std::string setValue(const Key& key, const std::string& args);
  std::string services(const Key& key, const std::string& args);
  std::string stop(const Key& key, const std::string& args);
  std::string state() const;

  void release(Key key, bool tellScript);
  void releaseUser(Key key, int link, bool tellScript);
  void releaseSubscription(Key key, bool tellScript);
  void enableNotifications(int link, uint16_t config, uint16_t handle, uint16_t value, bool retried);
  void addToSubscription(const Key& key, int link, const GattClient::Characteristic& c);
  void readSecured(int link, uint16_t handle, bool retried, std::function<void(bool ok, const Bytes& value)> done);
  void failSubscription(int link, uint16_t handle, uint8_t code);
  void deliver(const Key& key, Subscription& sub, const std::string& json);
  void dropLink(int link, const std::string& why);
  void retire(Link& link, const std::string& why);
  void startClient(int link);
  void reAdvertise();

  void onPower(Power p, const std::string& error);
  void onConnected(int link, bool ok, const std::string& error);
  void onAccepted(int link, const Address& peer);
  void onDisconnected(int link);
  void onAtt(int link, const uint8_t* pdu, std::size_t n);
  void onSecured(int link, bool ok, const std::string& error);
  void flushServer();
  void toListeners(const Address& central, bool connected);

  Link* linkOf(const Key& key, const std::string& args, std::string& error);
  std::size_t count(const std::string& script, Resource what) const;
  void emit(const Key& key, bool done, const std::string& json);
  void ensurePower();

  BleRadio& radio_;
  Sink sink_;
  Limits limits_;
  GattServer server_;
  Power power_ = Power::Off;
  int64_t now_ = 0;
  BleScanner scanner_;
  int64_t idleSince_ = -1;
  int64_t retryAt_ = -1;

  std::vector<Pending> pending_;
  std::map<Key, int> users_;
  std::map<Key, Subscription> subscriptions_;
  std::map<Key, int> adverts_;
  std::map<Key, Served> served_;
  std::map<int, Link> links_;
  // Clients whose link went away while one of their own callbacks was running; freed in tick().
  std::vector<std::unique_ptr<GattClient>> retired_;
  std::map<int, std::pair<bool, std::pair<Bytes, Bytes>>> advertisedWith_;
};

}
