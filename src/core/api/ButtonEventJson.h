#pragma once
#include <string>

#include "core/api/JsonWriter.h"

namespace awtrix::api {
// Body of the buttonCallback webhook, identical on every board:
// {"button":"left","state":true,"uid":"dcda0c29dcb8"}
inline std::string buttonEventJson(const char* button, bool state, const std::string& uid) {
  std::string out;
  JsonWriter w(out);
  w.beginObject().member("button", button).member("state", state).member("uid", uid).endObject();
  return out;
}
}
