#include "persistence/NvsSettings.h"

#include <Preferences.h>

#include <string>

#include "core/SettingsBlob.h"

namespace awtrix {
namespace nvs {

namespace {
const char* kNs = "awtrix-ng";
// All settings live in one JSON blob under a single key: they change far more often than the
// device config, and one blob costs one NVS write instead of dozens.
const char* kKey = "settings";
}

void loadSettings(Settings& s) {
  Preferences p;
  if (!p.begin(kNs, true)) return;
  const String blob = p.getString(kKey, "");
  p.end();
  settingsblob::apply(s, std::string_view(blob.c_str(), blob.length()));
}

void saveSettings(const Settings& s) {
  const std::string out = settingsblob::encode(s);
  Preferences p;
  p.begin(kNs, false);
  p.putString(kKey, out.c_str());
  p.end();
}

}
}
