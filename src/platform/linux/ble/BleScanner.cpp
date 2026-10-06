#include "platform/linux/ble/BleScanner.h"

#include <algorithm>

#include "platform/linux/ble/AdvData.h"
#include "platform/linux/ble/HubJson.h"

namespace awtrix::ble {
using namespace hub_detail;
using api::JsonReader;
namespace {
constexpr std::size_t kSeenMax = 128;

// Keeps a scan's dedupe map from growing with every address a phone rotates through: entries
// whose window has passed block nothing, and past that the oldest go first.
void prune(std::map<std::array<uint8_t, 6>, int64_t>& sent, int64_t now, int64_t window) {
  if (sent.size() <= kSeenMax) return;
  for (auto it = sent.begin(); it != sent.end();) it = now - it->second >= window ? sent.erase(it) : std::next(it);
  while (sent.size() > kSeenMax)
    sent.erase(std::min_element(sent.begin(), sent.end(), [](const auto& a, const auto& b) { return a.second < b.second; }));
}

}

std::string BleScanner::start(const ResourceKey& key, const std::string& args) {
  if (scans_.count(key)) return error("already scanning");
  const auto count = std::count_if(scans_.begin(), scans_.end(),
                                  [&](const auto& item) { return item.first.script == key.script; });
  if (static_cast<std::size_t>(count) >= limits_.perScript) return error("too many scans");
  Scan s;
  s.active = api::memberFlag(args, "active");
  const JsonReader uuids = api::memberValue(args, "uuid");
  if (api::present(uuids) && !uuids.isNull() && !readUuids(uuids, s.uuids)) return error("bad uuid");
  if (!api::memberText(args, "addr").empty()) {
    if (!parseAddress(args, s.address)) return error("bad address");
    s.byAddress = true;
  }
  s.name = api::memberText(args, "name");
  s.manufacturer = static_cast<int>(number(args, "mfg", -1));
  s.dedupeMs = std::max<long long>(0, number(args, "dedupe", 1000));
  s.background = api::memberFlag(args, "background");
  scans_[key] = std::move(s);
  apply();
  return kOk;
}

// A peer that is already linked no longer advertises: a scan looking for exactly that address
// hears about the link instead, and a connect to it then shares that link.
void BleScanner::reportLinked(const ResourceKey& key, const Address& peer) {
  const Scan& scan = scans_.at(key);
  std::string out;
  api::JsonWriter(out).beginObject().key("addr").value(peer.str()).key("random").value(scan.address.random)
      .key("linked").value(true).endObject();
  emit(key, out);
}

void BleScanner::apply() {
  const bool want = power_ == Power::On && !scans_.empty();
  const bool active = std::any_of(scans_.begin(), scans_.end(), [](const auto& e) { return e.second.active; });
  const bool background =
      std::all_of(scans_.begin(), scans_.end(), [](const auto& e) { return e.second.background; });
  if (want == scanning_ && (!want || (active == scanActive_ && background == scanBackground_))) return;
  scanning_ = want;
  scanActive_ = active;
  scanBackground_ = background;
  radio_.scan(want, active, background);
}

bool BleScanner::allowAdvertEvent(const std::string& script) {
  Bucket& b = buckets_[script];
  const double rate = limits_.eventsPerSecond;
  if (b.at == 0) b.tokens = rate;
  b.tokens = std::min(rate, b.tokens + static_cast<double>(now_ - b.at) * rate / 1000.0);
  b.at = now_;
  if (b.tokens < 1) return false;
  b.tokens -= 1;
  return true;
}

void BleScanner::onAdvert(const AdvReport& r) {
  if (scans_.empty()) return;
  Seen& seen = seen_[r.addr];
  if (r.eventType == 4) {
    seen.scanResponse = r.data;
  } else {
    seen.adv = r.data;
    seen.connectable = r.eventType == 0 || r.eventType == 1;
  }
  seen.at = now_;
  if (seen_.size() > kSeenMax) {
    auto oldest = std::min_element(seen_.begin(), seen_.end(),
                                   [](const auto& a, const auto& b) { return a.second.at < b.second.at; });
    if (oldest->first != r.addr) seen_.erase(oldest);
  }
  const Seen& s = seen_[r.addr];
  Bytes all = s.adv;
  all.insert(all.end(), s.scanResponse.begin(), s.scanResponse.end());
  const AdvFields f = parseAdv(all);
  std::string json;
  for (auto& [key, scan] : scans_) {
    if (scan.byAddress && scan.address.b != r.addr.b) continue;
    if (!scan.name.empty() && f.name.compare(0, scan.name.size(), scan.name) != 0) continue;
    if (scan.manufacturer >= 0 &&
        std::none_of(f.manufacturer.begin(), f.manufacturer.end(),
                     [&](const auto& m) { return m.first == scan.manufacturer; }))
      continue;
    if (!scan.uuids.empty()) {
      const bool hit = std::any_of(scan.uuids.begin(), scan.uuids.end(), [&](const Uuid& u) {
        return std::find(f.uuids.begin(), f.uuids.end(), u) != f.uuids.end() ||
               std::any_of(f.serviceData.begin(), f.serviceData.end(), [&](const auto& d) { return d.first == u; });
      });
      if (!hit) continue;
    }
    auto seenAt = scan.lastSent.find(r.addr.b);
    if (seenAt != scan.lastSent.end() && now_ - seenAt->second < scan.dedupeMs) continue;
    if (!allowAdvertEvent(key.script)) continue;
    scan.lastSent[r.addr.b] = now_;
    prune(scan.lastSent, now_, scan.dedupeMs);
    if (json.empty()) {
      api::JsonWriter w(json);
      w.beginObject().key("addr").value(r.addr.str()).key("random").value(r.addr.random).key("rssi")
          .value(static_cast<int>(r.rssi)).key("connectable").value(s.connectable);
      if (!f.name.empty()) w.key("name").value(f.name);
      if (!f.uuids.empty()) {
        w.key("uuids").beginArray();
        for (const Uuid& u : f.uuids) w.value(u.str());
        w.endArray();
      }
      if (!f.manufacturer.empty()) {
        w.key("mfg").beginArray();
        for (const auto& [id, data] : f.manufacturer)
          w.beginObject().key("id").value(static_cast<int>(id)).key("data").value(toHex(data)).endObject();
        w.endArray();
      }
      if (!f.serviceData.empty()) {
        w.key("svc").beginObject();
        for (const auto& [uuid, data] : f.serviceData) w.key(uuid.str().c_str()).value(toHex(data));
        w.endObject();
      }
      if (f.hasTxPower) w.key("tx").value(static_cast<int>(f.txPower));
      w.key("adv").value(toHex(s.adv)).key("rsp").value(toHex(s.scanResponse)).endObject();
    }
    emit(key, json);
  }
}

void BleScanner::emit(const ResourceKey& key, const std::string& json) {
  sink_(BleEvent{key.script, key.id, false, json});
}

bool BleScanner::erase(const ResourceKey& key) {
  if (!scans_.erase(key)) return false;
  apply();
  return true;
}

void BleScanner::collect(const std::string& script, std::set<ResourceKey>& keys) const {
  for (const auto& [key, scan] : scans_)
    if (key.script == script) keys.insert(key);
}

std::optional<Address> BleScanner::addressFilter(const ResourceKey& key) const {
  const auto found = scans_.find(key);
  if (found == scans_.end() || !found->second.byAddress) return std::nullopt;
  return found->second.address;
}

void BleScanner::reportLinked(const Address& peer) {
  for (const auto& [key, scan] : scans_)
    if (scan.byAddress && scan.address.b == peer.b) reportLinked(key, peer);
}

}  // namespace awtrix::ble
