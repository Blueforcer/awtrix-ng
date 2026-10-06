#include "platform/linux/ble/AdvData.h"

namespace awtrix::ble {
namespace {

enum : uint8_t {
  kFlags = 0x01,
  kUuid16Some = 0x02,
  kUuid16All = 0x03,
  kUuid32Some = 0x04,
  kUuid32All = 0x05,
  kUuid128Some = 0x06,
  kUuid128All = 0x07,
  kNameShort = 0x08,
  kNameComplete = 0x09,
  kTxPower = 0x0a,
  kSolicit16 = 0x14,
  kSolicit128 = 0x15,
  kServiceData16 = 0x16,
  kServiceData32 = 0x20,
  kServiceData128 = 0x21,
  kManufacturer = 0xff,
};

void addUuids(AdvFields& f, const uint8_t* p, std::size_t n, std::size_t width) {
  for (std::size_t i = 0; i + width <= n; i += width) {
    Uuid u;
    if (Uuid::fromWire(p + i, width, u)) f.uuids.push_back(u);
  }
}

void addServiceData(AdvFields& f, const uint8_t* p, std::size_t n, std::size_t width) {
  Uuid u;
  if (n < width || !Uuid::fromWire(p, width, u)) return;
  f.serviceData.emplace_back(u, Bytes(p + width, p + n));
}

bool field(Bytes& out, std::size_t room, uint8_t type, const Bytes& body) {
  if (out.size() + 2 + body.size() > room) return false;
  out.push_back(static_cast<uint8_t>(body.size() + 1));
  out.push_back(type);
  out.insert(out.end(), body.begin(), body.end());
  return true;
}

}

AdvFields parseAdv(const uint8_t* p, std::size_t n) {
  AdvFields f;
  std::size_t i = 0;
  while (i < n) {
    const std::size_t len = p[i];
    if (len == 0) break;
    if (i + 1 + len > n) break;
    const uint8_t type = p[i + 1];
    const uint8_t* body = p + i + 2;
    const std::size_t bn = len - 1;
    switch (type) {
      case kFlags:
        if (bn >= 1) f.hasFlags = true, f.flags = body[0];
        break;
      case kUuid16Some: case kUuid16All: addUuids(f, body, bn, 2); break;
      case kUuid32Some: case kUuid32All: addUuids(f, body, bn, 4); break;
      case kUuid128Some: case kUuid128All: addUuids(f, body, bn, 16); break;
      case kNameShort:
      case kNameComplete:
        if (f.name.empty() || type == kNameComplete) {
          f.name.assign(reinterpret_cast<const char*>(body), bn);
          f.completeName = type == kNameComplete;
        }
        break;
      case kTxPower:
        if (bn >= 1) f.hasTxPower = true, f.txPower = static_cast<int8_t>(body[0]);
        break;
      case kServiceData16: addServiceData(f, body, bn, 2); break;
      case kServiceData32: addServiceData(f, body, bn, 4); break;
      case kServiceData128: addServiceData(f, body, bn, 16); break;
      case kManufacturer:
        if (bn >= 2) f.manufacturer.emplace_back(le16(body), Bytes(body + 2, body + bn));
        break;
      default: break;
    }
    i += 1 + len;
  }
  return f;
}

bool buildAdv(const AdvPayload& payload, Bytes& adv, Bytes& scanResponse, std::string& error) {
  adv.clear();
  scanResponse.clear();
  const std::size_t advRoom = kAdvMax - kAdvFlagsCost;
  std::vector<std::pair<uint8_t, Bytes>> fields;
  Bytes shortUuids, longUuids;
  for (const Uuid& u : payload.uuids) {
    if (u.isShort()) put16(shortUuids, u.short16());
    else longUuids.insert(longUuids.end(), u.b.begin(), u.b.end());
  }
  if (!shortUuids.empty()) fields.emplace_back(kUuid16All, shortUuids);
  if (!longUuids.empty()) fields.emplace_back(kUuid128All, longUuids);
  Bytes shortSolicit, longSolicit;
  for (const Uuid& u : payload.solicit) {
    if (u.isShort()) put16(shortSolicit, u.short16());
    else longSolicit.insert(longSolicit.end(), u.b.begin(), u.b.end());
  }
  if (!shortSolicit.empty()) fields.emplace_back(kSolicit16, shortSolicit);
  if (!longSolicit.empty()) fields.emplace_back(kSolicit128, longSolicit);
  for (const auto& [id, data] : payload.manufacturer) {
    Bytes body;
    put16(body, id);
    body.insert(body.end(), data.begin(), data.end());
    fields.emplace_back(kManufacturer, body);
  }
  for (const auto& [uuid, data] : payload.serviceData) {
    Bytes body;
    uuid.appendWire(body);
    body.insert(body.end(), data.begin(), data.end());
    fields.emplace_back(uuid.isShort() ? kServiceData16 : kServiceData128, body);
  }
  for (const auto& [type, body] : fields) {
    if (field(adv, advRoom, type, body)) continue;
    if (field(scanResponse, kAdvMax, type, body)) continue;
    error = "advertising data does not fit 31 bytes";
    return false;
  }
  if (!payload.name.empty()) {
    const Bytes whole(payload.name.begin(), payload.name.end());
    if (!field(adv, advRoom, kNameComplete, whole) && !field(scanResponse, kAdvMax, kNameComplete, whole)) {
      Bytes* target = advRoom - adv.size() >= kAdvMax - scanResponse.size() ? &adv : &scanResponse;
      const std::size_t room = (target == &adv ? advRoom : kAdvMax) - target->size();
      if (room < 3) {
        error = "no room left for the name";
        return false;
      }
      field(*target, target == &adv ? advRoom : kAdvMax, kNameShort, Bytes(whole.begin(), whole.begin() + static_cast<long>(room - 2)));
    }
  }
  return true;
}

}
