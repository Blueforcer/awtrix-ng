#include "core/remote/ScriptHostStatus.h"

#include "core/api/JsonWriter.h"
#include "core/remote/HostAddress.h"

namespace awtrix {
namespace remote {

std::string scriptHostJson(const ScriptHostStatus& status) {
  std::string out;
  api::JsonWriter w(out);
  w.beginObject();
  w.key("mode").value(status.host.empty() ? "off" : "remote");
  w.key("host").value(status.host);
  w.key("link").value(status.link);
  if (!status.host.empty() && !status.key.empty()) w.key("key").value(status.key);
  w.key("offers").beginArray();
  for (const OfferEntry& offer : status.offers) {
    w.beginObject();
    w.key("name").value(offer.name);
    w.key("address").value(addressText(offer.address));
    w.key("port").value(static_cast<unsigned>(offer.port));
    w.endObject();
  }
  w.endArray();
  w.endObject();
  return out;
}

}
}
