#pragma once

#include <cstdint>
#include <string>
#include <utility>
#include <vector>

#include "platform/linux/ble/BleTypes.h"

namespace awtrix::ble {

// What an advertisement or scan response says, field by field. Unknown fields are skipped;
// the raw bytes stay available to scripts that decode a format of their own.
struct AdvFields {
  std::string name;
  bool completeName = false;
  bool hasFlags = false;
  uint8_t flags = 0;
  bool hasTxPower = false;
  int8_t txPower = 0;
  std::vector<Uuid> uuids;
  std::vector<std::pair<uint16_t, Bytes>> manufacturer;
  std::vector<std::pair<Uuid, Bytes>> serviceData;
};

// Malformed trailing bytes end the walk; everything read before them is kept.
AdvFields parseAdv(const uint8_t* p, std::size_t n);
inline AdvFields parseAdv(const Bytes& v) { return parseAdv(v.data(), v.size()); }

// The payload a script asks to send. The controller's limit is 31 bytes per packet; flags are
// added by the kernel and cost 3 of them in the advertisement itself.
struct AdvPayload {
  std::string name;
  std::vector<Uuid> uuids;
  std::vector<Uuid> solicit;
  std::vector<std::pair<uint16_t, Bytes>> manufacturer;
  std::vector<std::pair<Uuid, Bytes>> serviceData;
};

constexpr std::size_t kAdvMax = 31;
constexpr std::size_t kAdvFlagsCost = 3;

// Fills the advertisement first and spills whole fields into the scan response; the name goes
// last and is shortened when only part of it fits. False when a field fits neither packet.
bool buildAdv(const AdvPayload& payload, Bytes& adv, Bytes& scanResponse, std::string& error);

}
