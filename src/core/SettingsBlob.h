#pragma once

#include <string>
#include <string_view>

#include "core/Settings.h"
#include "core/api/JsonReader.h"
#include "core/api/JsonWriter.h"

namespace awtrix {
namespace settingsblob {

// Persisted schema tag; readers accept blobs from any schema version.
inline constexpr int kSchemaVersion = 1;

inline std::string encode(const Settings& settings) {
  std::string out;
  out.reserve(1536);
  api::JsonWriter writer(out);
  writer.beginObject();
  settings.writeMembers(writer);
  writer.member("schemaVersion", kSchemaVersion);
  writer.endObject();
  return out;
}

inline void apply(Settings& settings, std::string_view blob) {
  // Validate the whole blob before applying: truncated writes must not partly update settings.
  if (blob.empty() || !api::isWellFormed(blob)) return;
  settings.applyStored(api::JsonReader(blob));
}

}
}
