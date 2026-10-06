#pragma once

#include "core/api/JsonCoerce.h"
#include "core/api/JsonWriter.h"
#include "platform/linux/ble/BleTypes.h"

namespace awtrix::ble::hub_detail {

inline constexpr char kOk[] = "{\"ok\":true}";
long long number(const std::string& args, const char* name, long long fallback);
std::string error(const std::string& why);
bool isError(const std::string& json);
bool parseAddress(const std::string& args, Address& out);
bool readUuid(api::JsonReader reader, Uuid& out);
bool readUuids(api::JsonReader reader, std::vector<Uuid>& out);
bool readHex(api::JsonReader reader, Bytes& out);
std::string connected(uint16_t mtu);

}  // namespace awtrix::ble::hub_detail
