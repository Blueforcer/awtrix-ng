#include "platform/linux/ble/BleHub.h"

#include <algorithm>

#include "platform/linux/ble/AdvData.h"
#include "platform/linux/ble/Att.h"
#include "platform/linux/ble/HubJson.h"

namespace awtrix::ble {
using namespace hub_detail;
using api::JsonReader;

std::string BleHub::connect(const Key& key, const std::string& args) {
  if (users_.count(key)) return error("already connecting");
  Address peer;
  if (!parseAddress(args, peer)) return error("bad address");
  // One link per peer, whoever opened it: a watch that connects back to us by itself is read
  // over the link it made, as a second connection to it would be refused.
  for (auto& [id, link] : links_) {
    if (link.peer.b != peer.b) continue;
    users_[key] = id;
    if (link.client && link.ready) {
      link.users.insert(key);
      emit(key, false, connected(link.client->mtu()));
      return kOk;
    }
    link.waiting.insert(key);
    if (!link.client && !link.outgoing) startClient(id);
    return kOk;
  }
  if (count(key.script, kLinks) >= limits_.linksPerScript) return error("too many connections");
  const std::size_t outgoing =
      static_cast<std::size_t>(std::count_if(links_.begin(), links_.end(), [](const auto& e) { return e.second.outgoing; }));
  if (outgoing >= limits_.links) return error("too many connections");
  const int id = radio_.connect(peer, api::memberFlag(args, "secure") ? 2 : 1);
  if (id < 0) return error("connect failed");
  Link& link = links_[id];
  link.peer = peer;
  link.outgoing = true;
  link.waiting.insert(key);
  users_[key] = id;
  return kOk;
}

void BleHub::onConnected(int id, bool ok, const std::string& why) {
  auto it = links_.find(id);
  if (it == links_.end()) {
    if (ok) radio_.disconnect(id);
    return;
  }
  if (!ok) {
    dropLink(id, why.empty() ? "connect failed" : why);
    return;
  }
  server_.linkUp(id);
  startClient(id);
}

void BleHub::startClient(int id) {
  Link& link = links_[id];
  link.ready = false;
  link.client = std::make_unique<GattClient>([this, id](const Bytes& pdu) { return radio_.send(id, pdu); });
  link.client->onReady = [this, id](bool ready) {
    auto l = links_.find(id);
    if (l == links_.end()) return;
    if (!ready) {
      const std::string why = "discovery failed: " + l->second.client->error();
      if (log) log("ble: " + l->second.peer.str() + " " + why);
      radio_.disconnect(id);
      dropLink(id, why);
      return;
    }
    l->second.ready = true;
    const std::set<Key> waiting = std::move(l->second.waiting);
    l->second.waiting.clear();
    for (const Key& k : waiting) {
      l->second.users.insert(k);
      emit(k, false, connected(l->second.client->mtu()));
    }
  };
  link.client->onValue = [this, id](uint16_t handle, const Bytes& value) {
    auto l = links_.find(id);
    if (l == links_.end()) return;
    auto s = l->second.subscribers.find(handle);
    if (s == l->second.subscribers.end()) return;
    std::string out;
    api::JsonWriter(out).beginObject().key("data").value(toHex(value)).endObject();
    for (const Key& k : s->second) {
      auto sub = subscriptions_.find(k);
      if (sub != subscriptions_.end()) deliver(k, sub->second, out);
    }
  };
  link.client->start(now_);
}

// After turning a connect down the kernel goes on connecting to that peer by itself, and the link
// it makes arrives here as if the peer had connected to us: a connect still under way takes it.
void BleHub::onAccepted(int id, const Address& peer) {
  int pending = -1;
  for (const auto& [other, candidate] : links_)
    if (candidate.outgoing && !candidate.client && candidate.peer.b == peer.b) pending = other;
  Link& link = links_[id];
  link.peer = peer;
  server_.linkUp(id);
  idleSince_ = -1;
  if (pending >= 0) {
    radio_.disconnect(pending);
    link.outgoing = true;
    link.waiting = std::move(links_[pending].waiting);
    links_.erase(pending);
    for (const Key& k : link.waiting) users_[k] = id;
    startClient(id);
    return;
  }
  link.outgoing = false;
  link.ready = true;
  toListeners(peer, true);
  scanner_.reportLinked(peer);
}

void BleHub::onDisconnected(int id) { dropLink(id, "disconnected"); }

// The client may be running one of its own callbacks right now, so it is kept until the next
// tick. Its link callbacks are cut first, so answering the operations still queued on it cannot
// reach back into links_.
void BleHub::retire(Link& link, const std::string& why) {
  if (!link.client) return;
  retired_.push_back(std::move(link.client));
  GattClient& client = *retired_.back();
  client.onReady = nullptr;
  client.onValue = nullptr;
  client.abort(why);
}

void BleHub::dropLink(int id, const std::string& why) {
  auto it = links_.find(id);
  if (it == links_.end()) return;
  Link link = std::move(it->second);
  links_.erase(it);
  server_.linkDown(id);
  retire(link, why);
  std::set<Key> subscribed;
  for (const auto& [handle, keys] : link.subscribers) subscribed.insert(keys.begin(), keys.end());
  for (const Key& k : subscribed) {
    subscriptions_.erase(k);
    emit(k, true, "{\"end\":true}");
  }
  for (const Key& k : link.waiting) {
    users_.erase(k);
    emit(k, true, error(why));
  }
  for (const Key& k : link.users) {
    users_.erase(k);
    std::string out;
    api::JsonWriter(out).beginObject().key("disconnected").value(true).key("reason").value(why).endObject();
    emit(k, true, out);
  }
  for (const Key& k : link.pairing) emit(k, true, error(why));
  for (auto& retry : link.afterSecure) retry(false);
  if (!link.outgoing) toListeners(link.peer, false);
}

BleHub::Link* BleHub::linkOf(const Key& key, const std::string& args, std::string& why) {
  auto it = users_.find(Key{key.script, static_cast<uint32_t>(number(args, "conn", 0))});
  if (it == users_.end()) {
    why = "not connected";
    return nullptr;
  }
  auto l = links_.find(it->second);
  if (l == links_.end() || !l->second.ready || !l->second.client) {
    why = "not connected";
    return nullptr;
  }
  return &l->second;
}

std::string BleHub::pair(const Key& key, const std::string& args) {
  std::string why;
  Link* link = linkOf(key, args, why);
  if (!link) return error(why);
  const int id = users_[Key{key.script, static_cast<uint32_t>(number(args, "conn", 0))}];
  if (radio_.security(id) >= 2) {
    emit(key, true, "{\"paired\":true}");
    return kOk;
  }
  if (!radio_.secure(id, 2)) return error("pairing failed");
  link->pairing.push_back(key);
  return kOk;
}

void BleHub::onSecured(int id, bool ok, const std::string& why) {
  auto it = links_.find(id);
  if (it == links_.end()) return;
  const std::vector<Key> waiting = std::move(it->second.pairing);
  it->second.pairing.clear();
  auto retries = std::move(it->second.afterSecure);
  it->second.afterSecure.clear();
  for (const Key& k : waiting) emit(k, true, ok ? "{\"paired\":true}" : error(why.empty() ? "pairing failed" : why));
  for (auto& retry : retries) retry(ok);
}

}  // namespace awtrix::ble
