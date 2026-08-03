#include "core/backup/RestoreApplier.h"

#include <string>
#include <string_view>

#include "core/AssetPaths.h"
#include "core/api/JsonCoerce.h"
#include "core/api/JsonWriter.h"

namespace awtrix {
namespace backup {

namespace {
bool startsWith(const std::string& s, const char* prefix) { return s.rfind(prefix, 0) == 0; }
}

RestoreApplier::RestoreApplier(RestoreSink& sink) : sink_(sink) {}

RestoreApplier::Kind RestoreApplier::classify(const std::string& name) {
  if (name == "manifest.json") return Kind::Manifest;
  if (name == "config/wifi.json") return Kind::Wifi;
  if (name == "config/system.json") return Kind::System;
  if (name == "config/settings.json") return Kind::Settings;
  if (name == "apploop.json") return Kind::AppLoop;
  if (name == "radio.json") return Kind::RadioStations;
  if (startsWith(name, "ICONS/")) return Kind::Icon;
  if (startsWith(name, "MELODIES/")) return Kind::Melody;
  if (startsWith(name, "PALETTES/")) return Kind::Palette;
  if (startsWith(name, "SCRIPTS/")) return Kind::Script;
  return Kind::Unknown;
}

void RestoreApplier::fail(const std::string& message) {
  fatal_ = true;
  result_.ok = false;
  if (result_.error.empty()) result_.error = message;
  if (fileOpen_) {
    sink_.abortFile();
    fileOpen_ = false;
  }
}

void RestoreApplier::onEntryStart(const std::string& name, uint32_t) {
  if (fatal_) return;
  name_ = name;
  kind_ = classify(name);
  buffering_ = false;
  buf_.clear();
  fileOpen_ = false;
  fileRejected_ = false;
  contentChecked_ = false;

  // One forward pass over the archive, so the manifest cannot be looked up later: it has to be
  // the first entry, and the exporter always writes it first.
  if (!manifestOk_ && kind_ != Kind::Manifest) {
    fail("not an awtrix backup (manifest.json must be the first entry)");
    return;
  }

  switch (kind_) {
    case Kind::Manifest:
    case Kind::Wifi:
    case Kind::System:
    case Kind::Settings:
    case Kind::AppLoop:
    case Kind::RadioStations:
      buffering_ = true;
      return;
    case Kind::Icon:
    case Kind::Melody:
    case Kind::Palette:
    case Kind::Script: {
      const std::string path = "/" + name_;
      // Keeps a hand-made archive from writing outside the asset directories.
      if (!assets::isBackupWritable(path)) {
        ++result_.skipped;
        result_.warnings.push_back("skipped unsafe path '" + name_ + "'");
        fileRejected_ = true;
        return;
      }
      std::string err;
      if (sink_.beginFile(path, err)) {
        fileOpen_ = true;
      } else {
        fileRejected_ = true;
        result_.warnings.push_back("could not write " + path + (err.empty() ? "" : ": " + err));
      }
      return;
    }
    case Kind::Unknown:
      ++result_.skipped;
      result_.warnings.push_back("skipped unknown entry '" + name_ + "'");
      return;
  }
}

void RestoreApplier::onEntryData(const uint8_t* data, std::size_t n) {
  if (fatal_) return;
  if (buffering_) {
    buf_.append(reinterpret_cast<const char*>(data), n);
    return;
  }
  if (!fileOpen_) return;

  // Sniff the first chunk against the format the directory expects, so a mislabelled file is
  // dropped early. Only the first chunk is checked; asset files stream past too fast to buffer.
  const bool sniffable = kind_ == Kind::Icon || kind_ == Kind::Melody || kind_ == Kind::Palette;
  if (sniffable && !contentChecked_ && n > 0) {
    contentChecked_ = true;
    const assets::AssetKind ak = assets::kindFor("/" + name_);
    if (!assets::contentLooksValid(ak, data, static_cast<unsigned>(n))) {
      result_.warnings.push_back("skipped /" + name_ + ": content does not match " +
                                 assets::acceptedFormats(ak));
      sink_.abortFile();
      fileOpen_ = false;
      fileRejected_ = true;
      return;
    }
  }
  if (!sink_.writeFile(data, n)) {
    result_.warnings.push_back("write failed for /" + name_);
    sink_.abortFile();
    fileOpen_ = false;
    fileRejected_ = true;
  }
}

void RestoreApplier::onEntryEnd(bool crcOk) {
  if (fatal_) return;

  if (buffering_) {
    // A corrupt manifest aborts the whole restore; anything else corrupt is skipped with a
    // warning, so one bad icon does not cost the user their config.
    if (!crcOk) {
      if (kind_ == Kind::Manifest) {
        fail("backup manifest is corrupt (CRC mismatch)");
        return;
      }
      result_.warnings.push_back("skipped " + name_ + ": CRC mismatch");
      return;
    }
    std::string err;
    switch (kind_) {
      case Kind::Manifest: {
        if (!api::isWellFormed(buf_)) {
          fail("backup manifest is not valid JSON");
          return;
        }
        std::string app;
        api::memberValue(api::JsonReader(buf_), "app").appendString(app);
        const int fmt = api::coerceInt<int>(api::memberValue(api::JsonReader(buf_),
                                                             "backupFormat"));
        if (app != "awtrix-ng") {
          fail("not an awtrix-ng backup (manifest app=\"" + app + "\")");
          return;
        }
        if (fmt < 1 || fmt > kBackupFormat) {
          fail("unsupported backup format " + std::to_string(fmt));
          return;
        }
        manifestOk_ = true;
        return;
      }
      case Kind::Wifi: {
        if (!api::isWellFormed(buf_)) {
          result_.warnings.push_back("skipped wifi: invalid JSON");
          return;
        }
        std::string ssid, pass;
        api::JsonReader r{std::string_view(buf_)};
        if (r.isObject() && r.enterObject()) {
          while (r.nextMember()) {
            if (r.keyEquals("wifiSsid")) r.appendString(ssid);
            else if (r.keyEquals("wifiPass")) r.appendString(pass);
            if (!r.skipValue()) break;
          }
        }
        if (sink_.applyWifi(ssid, pass, err)) {
          ++result_.wifi;
        } else {
          result_.warnings.push_back("wifi not applied: " + err);
        }
        return;
      }
      case Kind::System:
        if (sink_.applySystem(buf_, err)) {
          ++result_.system;
        } else {
          result_.warnings.push_back("system config not applied: " + err);
        }
        return;
      case Kind::Settings:
        if (sink_.applySettings(buf_, err)) {
          ++result_.settings;
        } else {
          result_.warnings.push_back("settings not applied: " + err);
        }
        return;
      case Kind::AppLoop:
        if (sink_.applyAppLoop(buf_, err)) {
          ++result_.appLoop;
        } else {
          result_.warnings.push_back("app order not applied: " + err);
        }
        return;
      case Kind::RadioStations:
        if (sink_.applyRadioStations(buf_, err)) {
          ++result_.radioStations;
        } else {
          result_.warnings.push_back("radio stations not applied: " + err);
        }
        return;
      default:
        return;
    }
  }

  if (fileRejected_ || !fileOpen_) return;
  if (!crcOk) {
    result_.warnings.push_back("skipped /" + name_ + ": CRC mismatch");
    sink_.abortFile();
    fileOpen_ = false;
    return;
  }
  if (sink_.endFile()) {
    switch (kind_) {
      case Kind::Icon: ++result_.icons; break;
      case Kind::Melody: ++result_.melodies; break;
      case Kind::Palette: ++result_.palettes; break;
      case Kind::Script: ++result_.scripts; break;
      default: break;
    }
  } else {
    result_.warnings.push_back("could not finalize /" + name_);
  }
  fileOpen_ = false;
}

void RestoreApplier::onArchiveEnd() {
  if (fatal_) {
    result_.ok = false;
    return;
  }
  if (!manifestOk_) {
    result_.ok = false;
    if (result_.error.empty()) result_.error = "backup has no manifest.json";
    return;
  }
  sink_.commit();
  result_.ok = true;
}

std::string RestoreResult::toJson() const {
  std::string out;
  api::JsonWriter w(out);
  w.beginObject();
  w.member("ok", ok);
  if (!error.empty()) w.member("error", error);
  w.key("applied");
  w.beginObject();
  w.member("wifi", wifi);
  w.member("system", system);
  w.member("settings", settings);
  w.member("appLoop", appLoop);
  w.member("radioStations", radioStations);
  w.member("icons", icons);
  w.member("melodies", melodies);
  w.member("palettes", palettes);
  w.member("scripts", scripts);
  w.member("skipped", skipped);
  w.endObject();
  w.key("warnings");
  w.beginArray();
  for (const std::string& s : warnings) w.value(s);
  w.endArray();
  w.endObject();
  return out;
}

}
}
