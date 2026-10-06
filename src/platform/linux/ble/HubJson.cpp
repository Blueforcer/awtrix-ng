#include "platform/linux/ble/HubJson.h"

namespace awtrix::ble::hub_detail {
using api::JsonReader;

long long number(const std::string& args, const char* name, long long fallback) {
  const JsonReader r = api::memberValue(args, name);
  if (!api::present(r) || r.isNull()) return fallback;
  double d = 0;
  return api::coerceNumber(r, d) ? static_cast<long long>(d) : fallback;
}

std::string error(const std::string& why) {
  std::string out;
  api::JsonWriter(out).beginObject().key("error").value(why).endObject();
  return out;
}

bool isError(const std::string& json) { return json.rfind("{\"error\"", 0) == 0; }

bool parseAddress(const std::string& args, Address& out) {
  return Address::parse(api::memberText(args, "addr"), api::memberFlag(args, "random"), out);
}

bool readUuid(JsonReader r, Uuid& out) {
  std::string s;
  return r.appendString(s) && Uuid::parse(s, out);
}

bool readUuids(JsonReader r, std::vector<Uuid>& out) {
  if (r.isString()) {
    Uuid u;
    if (!readUuid(r, u)) return false;
    out.push_back(u);
    return true;
  }
  if (!r.enterArray()) return false;
  while (r.nextElement()) {
    Uuid u;
    if (!readUuid(r, u)) return false;
    out.push_back(u);
    if (!r.skipValue()) return false;
  }
  return r.ok();
}

bool readHex(JsonReader r, Bytes& out) {
  std::string s;
  return r.appendString(s) && fromHex(s, out);
}

std::string connected(uint16_t mtu) {
  std::string out;
  api::JsonWriter(out).beginObject().key("connected").value(true).key("mtu").value(static_cast<int>(mtu)).endObject();
  return out;
}

}  // namespace awtrix::ble::hub_detail
