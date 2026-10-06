#include "platform/linux/ble/BleHub.h"

#include <algorithm>

#include "platform/linux/ble/AdvData.h"
#include "platform/linux/ble/Att.h"
#include "platform/linux/ble/HubJson.h"

namespace awtrix::ble {
using namespace hub_detail;
using api::JsonReader;

std::string BleHub::services(const Key& key, const std::string& args) {
  std::string why;
  Link* link = linkOf(key, args, why);
  if (!link) return error(why);
  std::string out;
  api::JsonWriter w(out);
  w.beginObject().key("services").beginArray();
  for (const GattClient::Service& s : link->client->services()) {
    w.beginObject().key("uuid").value(s.uuid.str()).key("chars").beginArray();
    for (const GattClient::Characteristic& c : s.characteristics)
      w.beginObject().key("uuid").value(c.uuid.str()).key("props").value(static_cast<int>(c.props)).endObject();
    w.endArray().endObject();
  }
  w.endArray().endObject();
  return out;
}

std::string BleHub::gattOp(const Key& key, const std::string& op, const std::string& args) {
  std::string why;
  Link* link = linkOf(key, args, why);
  if (!link) return error(why);
  Uuid svc, chr;
  if (!Uuid::parse(api::memberText(args, "svc"), svc) || !Uuid::parse(api::memberText(args, "chr"), chr)) return error("bad uuid");
  const bool writing = op == "write";
  // With the same characteristic in several instances, the first that can do what is asked.
  const uint8_t wanted = writing ? (att::kPropWrite | att::kPropWriteNoResponse) : att::kPropRead;
  const auto all = link->client->findAll(svc, chr);
  if (all.empty()) return error("no such characteristic");
  const GattClient::Characteristic* c = all.front();
  for (const GattClient::Characteristic* candidate : all)
    if (candidate->props & wanted) {
      c = candidate;
      break;
    }
  const int id = users_[Key{key.script, static_cast<uint32_t>(number(args, "conn", 0))}];
  const uint16_t handle = c->value;
  Bytes data;
  if (writing && !readHex(api::memberValue(args, "data"), data)) return error("bad data");
  const bool withResponse = !api::memberFlag(args, "noresp");
  auto run = std::make_shared<std::function<void(bool)>>();
  // The operation owns itself only while the client or a pending pairing holds it.
  std::weak_ptr<std::function<void(bool)>> weak = run;
  *run = [this, key, id, handle, data, writing, withResponse, weak](bool retried) {
    auto l = links_.find(id);
    if (l == links_.end() || !l->second.client) {
      emit(key, true, error("not connected"));
      return;
    }
    auto run = weak.lock();
    auto done = [this, key, id, writing, retried, run](bool ok, uint8_t code, const Bytes& value) {
      const bool needsPairing = code == att::kInsufficientAuthentication || code == att::kInsufficientEncryption;
      if (!ok && needsPairing && !retried && radio_.security(id) < 2) {
        auto l = links_.find(id);
        if (l != links_.end() && radio_.secure(id, 2)) {
          l->second.afterSecure.push_back([this, key, run](bool secured) {
            if (secured) (*run)(true);
            else emit(key, true, error("pairing failed"));
          });
          return;
        }
      }
      if (!ok) {
        emit(key, true, error(code ? att::errorText(code) : links_.count(id) ? "no answer" : "disconnected"));
        return;
      }
      std::string out;
      if (writing) out = kOk;
      else api::JsonWriter(out).beginObject().key("data").value(toHex(value)).endObject();
      emit(key, true, out);
    };
    if (writing) l->second.client->write(handle, data, withResponse, done);
    else l->second.client->read(handle, done);
  };
  (*run)(false);
  return kOk;
}

std::string BleHub::subscribe(const Key& key, const std::string& args) {
  if (subscriptions_.count(key)) return error("already subscribed");
  if (count(key.script, kSubscriptions) >= limits_.subscriptionsPerScript) return error("too many subscriptions");
  std::string why;
  Link* link = linkOf(key, args, why);
  if (!link) return error(why);
  Uuid svc, chr;
  if (!Uuid::parse(api::memberText(args, "svc"), svc) || !Uuid::parse(api::memberText(args, "chr"), chr)) return error("bad uuid");
  // Every instance that can notify: values from any of them reach the script as one stream.
  std::vector<const GattClient::Characteristic*> targets;
  bool found = false;
  for (const GattClient::Characteristic* c : link->client->findAll(svc, chr)) {
    found = true;
    if (c->clientConfig && (c->props & (att::kPropNotify | att::kPropIndicate))) targets.push_back(c);
  }
  if (!found) return error("no such characteristic");
  if (targets.empty()) return error("cannot notify");
  // desc {"2908": "0101"} keeps the instances whose descriptor of that UUID holds exactly that
  // value, as a HID device tells its input reports apart by their Report Reference.
  Uuid descUuid;
  Bytes descValue;
  bool filtered = false;
  JsonReader desc = api::memberValue(args, "desc");
  if (api::present(desc) && !desc.isNull()) {
    if (!desc.enterObject() || !desc.nextMember()) return error("bad desc");
    std::string hex;
    if (!Uuid::parse(desc.key(), descUuid) || !desc.appendString(hex) || !fromHex(hex, descValue))
      return error("bad desc");
    filtered = true;
  }
  const int id = users_[Key{key.script, static_cast<uint32_t>(number(args, "conn", 0))}];
  Subscription& sub = subscriptions_[key];
  sub.link = id;
  sub.intervalMs = std::min<long long>(60000, std::max<long long>(0, number(args, "interval", 0)));
  if (!filtered) {
    for (const GattClient::Characteristic* c : targets) addToSubscription(key, id, *c);
    return kOk;
  }
  auto remaining = std::make_shared<std::size_t>(0);
  for (const GattClient::Characteristic* c : targets)
    if (c->descriptor(descUuid)) ++*remaining;
  if (*remaining == 0) {
    subscriptions_.erase(key);
    return error("no such characteristic");
  }
  for (const GattClient::Characteristic* c : targets) {
    const uint16_t handle = c->descriptor(descUuid);
    if (!handle) continue;
    const uint16_t value = c->value;
    readSecured(id, handle, false, [this, key, id, value, descValue, remaining](bool ok, const Bytes& content) {
      --*remaining;
      auto s = subscriptions_.find(key);
      auto l = links_.find(id);
      if (s == subscriptions_.end() || l == links_.end() || !l->second.client) return;
      if (ok && content == descValue) {
        for (const auto& svc : l->second.client->services())
          for (const GattClient::Characteristic& c : svc.characteristics)
            if (c.value == value) addToSubscription(key, id, c);
      }
      if (*remaining == 0 && s->second.handles.empty()) {
        subscriptions_.erase(s);
        emit(key, true, error("no such characteristic"));
      }
    });
  }
  return kOk;
}

void BleHub::addToSubscription(const Key& key, int id, const GattClient::Characteristic& c) {
  auto l = links_.find(id);
  auto s = subscriptions_.find(key);
  if (l == links_.end() || s == subscriptions_.end()) return;
  s->second.handles.push_back(c.value);
  std::set<Key>& subs = l->second.subscribers[c.value];
  subs.insert(key);
  if (subs.size() == 1) enableNotifications(id, c.clientConfig, c.value, (c.props & att::kPropNotify) ? 1 : 2, false);
}

// Reads a value, pairing first when the peer keeps it for paired centrals.
void BleHub::readSecured(int id, uint16_t handle, bool retried, std::function<void(bool ok, const Bytes& value)> done) {
  auto l = links_.find(id);
  if (l == links_.end() || !l->second.client) {
    done(false, {});
    return;
  }
  l->second.client->read(handle, [this, id, handle, retried, done](bool ok, uint8_t code, const Bytes& value) {
    const bool needsPairing = code == att::kInsufficientAuthentication || code == att::kInsufficientEncryption;
    auto link = links_.find(id);
    if (!ok && needsPairing && !retried && link != links_.end() && radio_.security(id) < 2 && radio_.secure(id, 2)) {
      link->second.afterSecure.push_back([this, id, handle, done](bool secured) {
        if (secured) readSecured(id, handle, true, done);
        else done(false, {});
      });
      return;
    }
    done(ok, value);
  });
}

void BleHub::deliver(const Key& key, Subscription& sub, const std::string& json) {
  if (sub.intervalMs > 0 && sub.lastSent >= 0 && now_ - sub.lastSent < sub.intervalMs) {
    sub.pending = json;
    return;
  }
  sub.lastSent = now_;
  sub.pending.clear();
  emit(key, false, json);
}

// A peer that keeps its values for paired centrals refuses the configuration write; pairing and
// writing it once more is what a phone does too.
void BleHub::enableNotifications(int id, uint16_t config, uint16_t handle, uint16_t value, bool retried) {
  auto l = links_.find(id);
  if (l == links_.end() || !l->second.client) return;
  Bytes on;
  put16(on, value);
  l->second.client->write(config, on, true, [this, id, config, handle, value, retried](bool ok, uint8_t code, const Bytes&) {
    if (ok) return;
    const bool needsPairing = code == att::kInsufficientAuthentication || code == att::kInsufficientEncryption;
    auto link = links_.find(id);
    if (needsPairing && !retried && link != links_.end() && radio_.security(id) < 2 && radio_.secure(id, 2)) {
      link->second.afterSecure.push_back([this, id, config, handle, value](bool secured) {
        if (secured) enableNotifications(id, config, handle, value, true);
        else failSubscription(id, handle, att::kInsufficientAuthentication);
      });
      return;
    }
    failSubscription(id, handle, code);
  });
}

void BleHub::failSubscription(int id, uint16_t handle, uint8_t code) {
  auto l = links_.find(id);
  if (l == links_.end()) return;
  auto s = l->second.subscribers.find(handle);
  if (s == l->second.subscribers.end()) return;
  const std::set<Key> keys = std::move(s->second);
  l->second.subscribers.erase(s);
  for (const Key& k : keys) {
    auto mine = subscriptions_.find(k);
    if (mine == subscriptions_.end()) continue;
    auto& handles = mine->second.handles;
    handles.erase(std::remove(handles.begin(), handles.end(), handle), handles.end());
    if (!handles.empty()) continue;
    subscriptions_.erase(mine);
    emit(k, true, error(code ? att::errorText(code) : "no answer"));
  }
}

void BleHub::releaseSubscription(Key key, bool tellScript) {
  auto it = subscriptions_.find(key);
  if (it == subscriptions_.end()) return;
  const Subscription sub = it->second;
  subscriptions_.erase(it);
  if (tellScript) emit(key, true, "{\"end\":true}");
  auto l = links_.find(sub.link);
  if (l == links_.end()) return;
  for (uint16_t handle : sub.handles) {
    auto s = l->second.subscribers.find(handle);
    if (s == l->second.subscribers.end()) continue;
    s->second.erase(key);
    if (!s->second.empty()) continue;
    l->second.subscribers.erase(s);
    if (!l->second.client) continue;
    for (const GattClient::Service& svc : l->second.client->services())
      for (const GattClient::Characteristic& c : svc.characteristics)
        if (c.value == handle && c.clientConfig)
          l->second.client->write(c.clientConfig, Bytes{0, 0}, true, [](bool, uint8_t, const Bytes&) {});
  }
}

void BleHub::releaseUser(Key key, int id, bool tellScript) {
  users_.erase(key);
  auto l = links_.find(id);
  if (l == links_.end()) return;
  std::vector<Key> mine;
  for (const auto& [k, sub] : subscriptions_)
    if (k.script == key.script && sub.link == id) mine.push_back(k);
  const bool lastOfScript = std::none_of(users_.begin(), users_.end(), [&](const auto& e) {
    return e.first.script == key.script && e.second == id;
  });
  if (lastOfScript)
    for (const Key& k : mine) releaseSubscription(k, tellScript);
  l->second.users.erase(key);
  l->second.waiting.erase(key);
  if (tellScript) {
    std::string out;
    api::JsonWriter(out).beginObject().key("disconnected").value(true).key("reason").value("closed").endObject();
    emit(key, true, out);
  }
  if (!l->second.users.empty() || !l->second.waiting.empty()) return;
  if (!l->second.outgoing) {
    retire(l->second, "disconnected");
    l->second.subscribers.clear();
    l->second.ready = true;
    return;
  }
  Link link = std::move(l->second);
  links_.erase(l);
  retire(link, "disconnected");
  server_.linkDown(id);
  radio_.disconnect(id);
}

}  // namespace awtrix::ble
