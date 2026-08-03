#include "persistence/NvsSettings.h"

#include <Preferences.h>

#include <string>

#include "core/api/JsonReader.h"
#include "core/api/JsonWriter.h"

namespace awtrix {
namespace nvs {

namespace {
const char* kNs = "awtrix-ng";
// All settings live in one JSON blob under a single key: they change far more often than the
// device config, and one blob costs one NVS write instead of dozens.
const char* kKey = "settings";
// Written for future migrations; nothing reads it back yet.
constexpr int kSchemaVersion = 1;
}

void loadSettings(Settings& s) {
  Preferences p;
  if (!p.begin(kNs, true)) return;
  const String blob = p.getString(kKey, "");
  p.end();
  if (blob.isEmpty()) return;
  const std::string_view text(blob.c_str(), blob.length());
  // Validate before applying — a blob truncated by a brownout mid-write would otherwise leave
  // the settings half updated.
  if (!api::isWellFormed(text)) return;
  s.applyRead(api::JsonReader(text));
}

void saveSettings(const Settings& s) {
  std::string out;
  out.reserve(1536);
  api::JsonWriter w(out);
  w.beginObject();
  s.writeMembers(w);
  w.member("schemaVersion", kSchemaVersion);
  w.endObject();
  Preferences p;
  p.begin(kNs, false);
  p.putString(kKey, out.c_str());
  p.end();
}

}
}
