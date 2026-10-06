#include "platform/linux/ble/BleHub.h"

#include <algorithm>

#include "platform/linux/ble/AdvData.h"
#include "platform/linux/ble/Att.h"
#include "platform/linux/ble/HubJson.h"

namespace awtrix::ble {
using namespace hub_detail;
using api::JsonReader;

std::string BleHub::advertise(const Key& key, const std::string& args) {
  if (adverts_.count(key)) return error("already advertising");
  if (count(key.script, kAdverts) >= limits_.advertsPerScript) return error("too many adverts");
  AdvPayload payload;
  payload.name = api::memberText(args, "name");
  const JsonReader uuids = api::memberValue(args, "uuids");
  if (api::present(uuids) && !uuids.isNull() && !readUuids(uuids, payload.uuids)) return error("bad uuid");
  const JsonReader solicit = api::memberValue(args, "solicit");
  if (api::present(solicit) && !solicit.isNull() && !readUuids(solicit, payload.solicit)) return error("bad uuid");
  JsonReader mfg = api::memberValue(args, "mfg");
  if (api::present(mfg) && mfg.enterArray()) {
    while (mfg.nextElement()) {
      Bytes data;
      const JsonReader entry = mfg;
      const long long id = number(std::string(entry.valueText()), "id", -1);
      if (id < 0 || id > 0xffff || !readHex(api::memberValue(entry, "data"), data)) return error("bad mfg");
      payload.manufacturer.emplace_back(static_cast<uint16_t>(id), std::move(data));
      if (!mfg.skipValue()) break;
    }
  }
  JsonReader svc = api::memberValue(args, "svc");
  if (api::present(svc) && svc.enterObject()) {
    while (svc.nextMember()) {
      Uuid u;
      Bytes data;
      if (!Uuid::parse(svc.key(), u) || !readHex(svc, data)) return error("bad service data");
      payload.serviceData.emplace_back(u, std::move(data));
      if (!svc.skipValue()) break;
    }
  }
  Bytes adv, rsp;
  std::string why;
  if (!buildAdv(payload, adv, rsp, why)) return error(why);
  std::set<int> used;
  for (const auto& [k, instance] : adverts_) used.insert(instance);
  int instance = 0;
  for (int i = 1; i <= radio_.advertisingSlots(); ++i)
    if (!used.count(i)) {
      instance = i;
      break;
    }
  if (!instance) return error("no advertising slot free");
  const bool connectable = !api::present(api::memberValue(args, "connectable")) || api::memberFlag(args, "connectable");
  if (!radio_.advertise(instance, connectable, adv, rsp, why)) return error(why.empty() ? "advertising failed" : why);
  adverts_[key] = instance;
  advertisedWith_[instance] = {connectable, {adv, rsp}};
  return kOk;
}

void BleHub::reAdvertise() {
  for (const auto& [key, instance] : adverts_) {
    const auto& how = advertisedWith_[instance];
    std::string why;
    radio_.advertise(instance, how.first, how.second.first, how.second.second, why);
  }
}

std::string BleHub::serve(const Key& key, const std::string& args) {
  if (served_.count(key)) return error("already serving");
  if (count(key.script, kServices) >= limits_.servicesPerScript) return error("too many services");
  Uuid uuid;
  if (!Uuid::parse(api::memberText(args, "uuid"), uuid)) return error("bad uuid");
  if (server_.hasService(uuid)) return error("service already served");
  std::vector<GattServer::CharacteristicSpec> specs;
  JsonReader chars = api::memberValue(args, "chars");
  if (!chars.enterArray()) return error("bad characteristics");
  while (chars.nextElement()) {
    const std::string entry(chars.valueText());
    GattServer::CharacteristicSpec c;
    if (!Uuid::parse(api::memberText(entry, "uuid"), c.uuid)) return error("bad uuid");
    for (char p : api::memberText(entry, "props")) {
      switch (p) {
        case 'r': c.props |= att::kPropRead; break;
        case 'w': c.props |= att::kPropWrite; break;
        case 'x': c.props |= att::kPropWriteNoResponse; break;
        case 'n': c.props |= att::kPropNotify; break;
        case 'i': c.props |= att::kPropIndicate; break;
        case 'e': c.encrypted = true; break;
        default: return error(std::string("bad property ") + p);
      }
    }
    const JsonReader value = api::memberValue(entry, "value");
    if (api::present(value) && !value.isNull() && !readHex(value, c.value)) return error("bad value");
    specs.push_back(std::move(c));
    if (!chars.skipValue()) break;
  }
  if (specs.empty()) return error("a service needs a characteristic");
  std::vector<uint16_t> handles;
  const int service = server_.addService(uuid, specs, handles);
  if (!service) return error("attribute table full");
  Served s;
  s.service = service;
  for (std::size_t i = 0; i < handles.size(); ++i) s.byHandle[handles[i]] = specs[i].uuid;
  served_[key] = std::move(s);
  flushServer();
  if (power_ != Power::On) ensurePower();
  return kOk;
}

std::string BleHub::setValue(const Key& key, const std::string& args) {
  auto it = served_.find(Key{key.script, static_cast<uint32_t>(number(args, "svc", 0))});
  if (it == served_.end()) return error("not serving");
  Uuid chr;
  if (!Uuid::parse(api::memberText(args, "chr"), chr)) return error("bad uuid");
  Bytes data;
  if (!readHex(api::memberValue(args, "data"), data)) return error("bad data");
  for (const auto& [handle, uuid] : it->second.byHandle) {
    if (uuid != chr) continue;
    if (!server_.setValue(handle, data)) return error("value too long");
    server_.publish(handle);
    flushServer();
    return kOk;
  }
  return error("no such characteristic");
}

void BleHub::flushServer() {
  for (auto& [link, pdu] : server_.drain([this](int link) { return radio_.security(link); }))
    radio_.send(link, pdu);
}

// A central is news to whoever it may have come for: the services it can use and the adverts
// that told it about us.
void BleHub::toListeners(const Address& central, bool connected) {
  std::string out;
  api::JsonWriter(out).beginObject().key("central").value(central.str()).key("connected").value(connected)
      .key("random").value(central.random).endObject();
  for (const auto& [key, served] : served_) emit(key, false, out);
  for (const auto& [key, instance] : adverts_) emit(key, false, out);
}

}  // namespace awtrix::ble
