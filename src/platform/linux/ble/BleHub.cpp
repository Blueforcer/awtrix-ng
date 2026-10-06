#include "platform/linux/ble/BleHub.h"

#include <algorithm>

#include "core/api/JsonCoerce.h"
#include "core/api/JsonReader.h"
#include "core/api/JsonWriter.h"
#include "platform/linux/ble/AdvData.h"
#include "platform/linux/ble/Att.h"
#include "platform/linux/ble/HubJson.h"

namespace awtrix::ble {
using namespace hub_detail;
using api::JsonReader;

BleHub::BleHub(BleRadio& radio, Sink sink) : BleHub(radio, std::move(sink), Limits{}) {}

BleHub::BleHub(BleRadio& radio, Sink sink, Limits limits)
    : radio_(radio), sink_(std::move(sink)), limits_(limits),
      scanner_(radio_, sink_, power_, now_, {limits.scansPerScript, limits.advertEventsPerSecond}) {
  BleRadio::Events e;
  e.power = [this](Power p, const std::string& why) { onPower(p, why); };
  e.advert = [this](const AdvReport& r) { scanner_.onAdvert(r); };
  e.connected = [this](int link, bool ok, const std::string& why) { onConnected(link, ok, why); };
  e.accepted = [this](int link, const Address& peer) { onAccepted(link, peer); };
  e.disconnected = [this](int link) { onDisconnected(link); };
  e.att = [this](int link, const uint8_t* p, std::size_t n) { onAtt(link, p, n); };
  e.secured = [this](int link, bool ok, const std::string& why) { onSecured(link, ok, why); };
  radio_.setEvents(std::move(e));
  server_.onWrite = [this](int link, uint16_t handle, const Bytes& value) {
    for (auto& [key, served] : served_) {
      auto it = served.byHandle.find(handle);
      if (it == served.byHandle.end()) continue;
      std::string out;
      api::JsonWriter(out).beginObject().key("write").value(it->second.str()).key("data").value(toHex(value))
          .key("addr").value(radio_.peer(link).str()).endObject();
      emit(key, false, out);
    }
  };
  server_.onSubscribe = [this](int link, uint16_t handle, uint16_t config) {
    for (auto& [key, served] : served_) {
      auto it = served.byHandle.find(handle);
      if (it == served.byHandle.end()) continue;
      std::string out;
      api::JsonWriter(out).beginObject().key("subscribe").value(it->second.str()).key("on").value(config != 0)
          .key("addr").value(radio_.peer(link).str()).endObject();
      emit(key, false, out);
    }
  };
}

void BleHub::emit(const Key& key, bool done, const std::string& json) {
  BleEvent e;
  e.script = key.script;
  e.id = key.id;
  e.done = done;
  e.json = json;
  sink_(std::move(e));
}

bool BleHub::idle() const {
  return pending_.empty() && scanner_.empty() && users_.empty() && adverts_.empty() && served_.empty() &&
         links_.empty();
}

bool BleHub::linked(const Address& peer) const {
  return std::any_of(links_.begin(), links_.end(), [&](const auto& e) { return e.second.peer.b == peer.b; });
}

void BleHub::disconnect(const Address& peer) {
  for (const auto& [id, link] : links_)
    if (link.peer.b == peer.b) radio_.disconnect(id);
}

void BleHub::ensurePower() {
  if (power_ == Power::Off || power_ == Power::Failed) {
    power_ = Power::Starting;
    radio_.power(true);
  }
}

std::string BleHub::call(const std::string& script, const std::string& op, const std::string& args, uint32_t id,
                         int64_t nowMs) {
  now_ = nowMs;
  const Key key{script, id};
  if (op == "state") return state();
  if (op == "bonds") {
    std::string out;
    api::JsonWriter w(out);
    w.beginObject().key("bonds").beginArray();
    for (const Address& a : radio_.bonds()) w.value(a.str());
    w.endArray().endObject();
    return out;
  }
  if (op == "forget") {
    Address a;
    if (!parseAddress(args, a)) return error("bad address");
    return radio_.forget(a) ? kOk : error("no such bond");
  }
  if (op == "stop") return stop(key, args);
  if (op == "set") return setValue(key, args);
  if (op == "services") return services(key, args);
  if (power_ != Power::On) {
    const bool needsRadio = op == "scan" || op == "connect" || op == "advertise" || op == "serve";
    if (!needsRadio) return error("not connected");
    if (op == "serve") return serve(key, args);
    pending_.push_back(Pending{key, op, args});
    ensurePower();
    idleSince_ = -1;
    return kOk;
  }
  idleSince_ = -1;
  return execute(key, op, args);
}

std::string BleHub::execute(const Key& key, const std::string& op, const std::string& args) {
  if (op == "scan") return startScan(key, args);
  if (op == "connect") return connect(key, args);
  if (op == "disconnect") {
    auto it = users_.find(Key{key.script, static_cast<uint32_t>(number(args, "conn", 0))});
    if (it == users_.end()) return error("not connected");
    releaseUser(it->first, it->second, true);
    return kOk;
  }
  if (op == "read" || op == "write") return gattOp(key, op, args);
  if (op == "subscribe") return subscribe(key, args);
  if (op == "pair") return pair(key, args);
  if (op == "advertise") return advertise(key, args);
  if (op == "serve") return serve(key, args);
  return error("unknown operation " + op);
}

std::string BleHub::state() const {
  static const char* const kNames[] = {"off", "starting", "on", "failed"};
  std::string out;
  api::JsonWriter w(out);
  w.beginObject().key("state").value(kNames[static_cast<int>(power_)]);
  if (power_ == Power::On) w.key("addr").value(radio_.address().str());
  w.endObject();
  return out;
}

std::size_t BleHub::count(const std::string& script, Resource what) const {
  auto mine = [&](const auto& m) {
    return static_cast<std::size_t>(std::count_if(m.begin(), m.end(), [&](const auto& e) { return e.first.script == script; }));
  };
  switch (what) {
    case kSubscriptions: return mine(subscriptions_);
    case kAdverts: return mine(adverts_);
    case kServices: return mine(served_);
    default: {
      std::set<int> links;
      for (const auto& [key, link] : users_)
        if (key.script == script) links.insert(link);
      return links.size();
    }
  }
}

std::string BleHub::startScan(const Key& key, const std::string& args) {
  const std::string result = scanner_.start(key, args);
  if (!isError(result))
    if (const auto peer = scanner_.addressFilter(key); peer && linked(*peer)) scanner_.reportLinked(key, *peer);
  return result;
}

void BleHub::onAtt(int id, const uint8_t* p, std::size_t n) {
  if (n == 0) return;
  if (att::toServer(p[0])) {
    const Bytes rsp = server_.handle(id, p, n, radio_.security(id));
    if (!rsp.empty()) radio_.send(id, rsp);
    flushServer();
    return;
  }
  auto it = links_.find(id);
  if (it != links_.end() && it->second.client) it->second.client->receive(p, n, now_);
}

std::string BleHub::stop(const Key& key, const std::string& args) {
  const Key target{key.script, static_cast<uint32_t>(number(args, "id", 0))};
  if (scanner_.erase(target)) {
    emit(target, true, "{\"end\":true}");
    return kOk;
  }
  if (subscriptions_.count(target)) {
    releaseSubscription(target, true);
    return kOk;
  }
  if (users_.count(target)) {
    releaseUser(target, users_[target], true);
    return kOk;
  }
  if (adverts_.count(target) || served_.count(target)) {
    release(target, true);
    return kOk;
  }
  for (auto it = pending_.begin(); it != pending_.end(); ++it)
    if (it->key.script == target.script && it->key.id == target.id) {
      pending_.erase(it);
      emit(target, true, "{\"end\":true}");
      return kOk;
    }
  return error("nothing running under that handle");
}

void BleHub::release(Key key, bool tellScript) {
  scanner_.erase(key);
  releaseSubscription(key, false);
  auto u = users_.find(key);
  if (u != users_.end()) releaseUser(key, u->second, false);
  auto a = adverts_.find(key);
  if (a != adverts_.end()) {
    radio_.stopAdvertising(a->second);
    advertisedWith_.erase(a->second);
    adverts_.erase(a);
  }
  auto s = served_.find(key);
  if (s != served_.end()) {
    server_.removeService(s->second.service);
    served_.erase(s);
    flushServer();
  }
  if (tellScript) emit(key, true, "{\"end\":true}");
}

void BleHub::forget(const std::string& script, int64_t nowMs) {
  now_ = nowMs;
  pending_.erase(std::remove_if(pending_.begin(), pending_.end(), [&](const Pending& p) { return p.key.script == script; }),
                 pending_.end());
  std::set<Key> keys;
  auto collect = [&](const auto& m) {
    for (const auto& e : m)
      if (e.first.script == script) keys.insert(e.first);
  };
  scanner_.collect(script, keys);
  collect(subscriptions_);
  collect(users_);
  collect(adverts_);
  collect(served_);
  for (const Key& k : keys) release(k, false);
  scanner_.forgetBudget(script);
}

void BleHub::onPower(Power p, const std::string& why) {
  const Power was = power_;
  power_ = p;
  if (p == Power::On) {
    retryAt_ = -1;
    scanner_.resetRadio();
    const std::vector<Pending> pending = std::move(pending_);
    pending_.clear();
    for (const Pending& op : pending) {
      const std::string result = execute(op.key, op.op, op.args);
      if (isError(result)) emit(op.key, true, result);
    }
    scanner_.apply();
    reAdvertise();
    return;
  }
  if (p == Power::Starting) return;
  const std::string message = why.empty() ? "Bluetooth is off" : why;
  for (const Pending& op : pending_) emit(op.key, true, error(message));
  pending_.clear();
  std::vector<int> ids;
  for (const auto& [id, link] : links_) ids.push_back(id);
  for (int id : ids) dropLink(id, message);
  scanner_.resetRadio();
  if (was == Power::On || p == Power::Failed) {
    if (!scanner_.empty() || !adverts_.empty() || !served_.empty()) retryAt_ = now_ + kPowerRetryMs;
  }
}

void BleHub::tick(int64_t nowMs) {
  now_ = nowMs;
  retired_.clear();
  for (auto& [key, sub] : subscriptions_)
    if (!sub.pending.empty() && now_ - sub.lastSent >= sub.intervalMs) {
      std::string json = std::move(sub.pending);
      sub.pending.clear();
      sub.lastSent = now_;
      emit(key, false, json);
    }
  std::vector<int> ids;
  for (const auto& [id, link] : links_) ids.push_back(id);
  // A client's timeout can end discovery, and that drops its link; links_ changes underneath.
  for (int id : ids) {
    auto it = links_.find(id);
    if (it != links_.end() && it->second.client) it->second.client->tick(nowMs);
  }
  std::vector<int> broken;
  for (const auto& [id, link] : links_)
    if (link.client && link.client->broken() && link.ready) broken.push_back(id);
  for (int id : broken) {
    const std::string why = links_[id].client->error();
    if (log) log("ble: dropping " + links_[id].peer.str() + ": " + why);
    radio_.disconnect(id);
    dropLink(id, why);
  }
  if (retryAt_ >= 0 && nowMs >= retryAt_) {
    retryAt_ = -1;
    ensurePower();
  }
  if (power_ == Power::On && idle()) {
    if (idleSince_ < 0) idleSince_ = nowMs;
    if (nowMs - idleSince_ >= kIdleOffMs) {
      idleSince_ = -1;
      power_ = Power::Off;
      scanner_.resetRadio();
      radio_.power(false);
    }
  } else {
    idleSince_ = -1;
  }
}

}
