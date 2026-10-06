#include "core/backup/RestoreApplier.h"

#include <string>
#include <string_view>
#include <utility>

#include "core/AssetPaths.h"
#include "core/icons/IconOrigins.h"
#include "core/api/JsonCoerce.h"
#include "core/api/JsonWriter.h"

namespace awtrix {
namespace backup {

namespace {
bool startsWith(const std::string& s, const char* prefix) { return s.rfind(prefix, 0) == 0; }
}

RestoreApplier::RestoreApplier(RestoreSink& sink) : sink_(sink) {}

RestoreApplier::~RestoreApplier() {
  if (fileOpen_) sink_.abortFile();
}

void RestoreApplier::abort(const std::string& reason) {
  if (!completed_) fail(reason.empty() ? "backup upload aborted" : reason);
}

RestoreApplier::Kind RestoreApplier::classify(const std::string& name) {
  if (name == "manifest.json") return Kind::Manifest;
  if (name == "config/wifi.json") return Kind::Wifi;
  if (name == "config/system.json") return Kind::System;
  if (name == "config/settings.json") return Kind::Settings;
  if (name == "config/icon-origins.json") return Kind::IconOrigins;
  if (name == "apploop.json") return Kind::AppLoop;
  if (name == "radio.json") return Kind::RadioStations;
  if (startsWith(name, "ICONS/")) return Kind::Icon;
  if (startsWith(name, "MELODIES/")) return Kind::Melody;
  if (startsWith(name, "PALETTES/")) return Kind::Palette;
  if (startsWith(name, "MP3/")) return Kind::Mp3;
  // A script's sound is checked and counted like any other MP3; the path check that follows keeps
  // everything else under SCRIPTS/ flat.
  if (startsWith(name, "SCRIPTS/"))
    return sound::isScriptMp3Path("/" + name) ? Kind::Mp3 : Kind::Script;
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

void RestoreApplier::skip(std::string message) {
  ++result_.skipped;
  if (result_.warnings.size() < kMaxRestoreWarnings)
    result_.warnings.push_back(std::move(message));
  else if (result_.warnings.size() == kMaxRestoreWarnings)
    result_.warnings.push_back("more warnings omitted");
}

void RestoreApplier::onEntryStart(const std::string& name, uint32_t size) {
  if (fatal_ || completed_) return;
  if (fileOpen_) {
    fail("entry before previous file ended");
    return;
  }
  name_ = name;
  kind_ = classify(name);
  buffering_ = false;
  buf_.clear();
  fileOpen_ = false;
  fileRejected_ = false;
  validateContent_ = false;
  metadataTooLarge_ = false;
  metadataLimit_ = kind_ == Kind::Manifest ? kMaxRestoreManifestBytes
                   : kind_ == Kind::IconOrigins ? iconorigins::kMaxBytes
                                              : kMaxRestoreMetadataBytes;

  // One forward pass over the archive, so the manifest cannot be looked up later: it has to be
  // the first entry, and the exporter always writes it first.
  if (!manifestOk_ && kind_ != Kind::Manifest) {
    fail("not an awtrix backup");
    return;
  }

  switch (kind_) {
    case Kind::Manifest:
    case Kind::Wifi:
    case Kind::System:
    case Kind::Settings:
    case Kind::AppLoop:
    case Kind::RadioStations:
    case Kind::IconOrigins:
      buffering_ = true;
      if (size > metadataLimit_) {
        metadataTooLarge_ = true;
        if (kind_ == Kind::Manifest) fail("manifest over 4 KiB");
      }
      return;
    case Kind::Icon:
    case Kind::Melody:
    case Kind::Palette:
    case Kind::Mp3:
    case Kind::Script: {
      const std::string path = "/" + name_;
      // Keeps a hand-made archive from writing outside the asset directories.
      if (!assets::isBackupWritable(path)) {
        skip("skipped unsafe path '" + name_ + "'");
        fileRejected_ = true;
        return;
      }
      validateContent_ = kind_ != Kind::Script;
      if (validateContent_) contentValidator_.reset(path);
      std::string err;
      if (sink_.beginFile(path, err)) {
        fileOpen_ = true;
      } else {
        fileRejected_ = true;
        skip("write failed " + path + (err.empty() ? "" : ": " + err));
      }
      return;
    }
    case Kind::Unknown:
      skip("skipped unknown entry '" + name_ + "'");
      return;
  }
}

void RestoreApplier::onEntryData(const uint8_t* data, std::size_t n) {
  if (fatal_) return;
  if (buffering_) {
    if (metadataTooLarge_ || n > metadataLimit_ - buf_.size()) {
      metadataTooLarge_ = true;
      buf_.clear();
      if (kind_ == Kind::Manifest) fail("manifest over 4 KiB");
      return;
    }
    buf_.append(reinterpret_cast<const char*>(data), n);
    return;
  }
  if (!fileOpen_) return;

  // Prefixes may span HTTP chunks. The shared validator retains only four prefix bytes or
  // bounded RTTTL text, and checks palettes through their final byte.
  if (validateContent_ && !contentValidator_.append(data, n)) {
    skip("skipped /" + name_ + ": expected " +
        assets::acceptedFormats(assets::kindFor("/" + name_)));
    sink_.abortFile();
    fileOpen_ = false;
    fileRejected_ = true;
    return;
  }
  if (!sink_.writeFile(data, n)) {
    skip("write failed /" + name_);
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
        fail("manifest CRC mismatch");
        return;
      }
      skip("skipped " + name_ + ": CRC mismatch");
      return;
    }
    if (metadataTooLarge_) {
      skip(kind_ == Kind::IconOrigins ? "skipped icon origins: over 16 KiB"
                                     : "skipped " + name_ + ": over 64 KiB");
      return;
    }
    std::string err;
    switch (kind_) {
      case Kind::Manifest: {
        if (!api::isWellFormed(buf_)) {
          fail("manifest invalid JSON");
          return;
        }
        std::string app;
        api::memberValue(api::JsonReader(buf_), "app").appendString(app);
        const int fmt = api::coerceInt<int>(api::memberValue(api::JsonReader(buf_),
                                                             "backupFormat"));
        if (app != "awtrix-ng") {
          fail("not an awtrix-ng backup");
          return;
        }
        if (fmt < 1 || fmt > kBackupFormat) {
          fail("unsupported backup format " + std::to_string(fmt));
          return;
        }
        manifestOk_ = true;
        return;
      }
      case Kind::IconOrigins: {
        std::vector<iconorigins::Record> records;
        if (!iconorigins::parseCollection(buf_, records)) {
          skip("skipped icon origins: invalid");
          return;
        }
        pendingIconOrigins_ = std::move(buf_);
        return;
      }
      case Kind::Wifi: {
        if (!api::isWellFormed(buf_)) {
          skip("skipped wifi: invalid JSON");
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
          skip("wifi not applied: " + err);
        }
        return;
      }
      case Kind::System:
        if (sink_.applySystem(buf_, err)) {
          ++result_.system;
        } else {
          skip("system config not applied: " + err);
        }
        return;
      case Kind::Settings:
        if (sink_.applySettings(buf_, err)) {
          ++result_.settings;
        } else {
          skip("settings not applied: " + err);
        }
        return;
      case Kind::AppLoop:
        if (sink_.applyAppLoop(buf_, err)) {
          ++result_.appLoop;
        } else {
          skip("app order not applied: " + err);
        }
        return;
      case Kind::RadioStations:
        if (sink_.applyRadioStations(buf_, err)) {
          ++result_.radioStations;
        } else {
          skip("radio stations not applied: " + err);
        }
        return;
      default:
        return;
    }
  }

  if (fileRejected_ || !fileOpen_) return;
  if (!crcOk) {
    skip("skipped /" + name_ + ": CRC mismatch");
    sink_.abortFile();
    fileOpen_ = false;
    return;
  }
  if (validateContent_ && !contentValidator_.finish()) {
    skip("skipped /" + name_ + ": expected " +
        assets::acceptedFormats(assets::kindFor("/" + name_)));
    sink_.abortFile();
    fileOpen_ = false;
    return;
  }
  if (sink_.endFile()) {
    switch (kind_) {
      case Kind::Icon: ++result_.icons; break;
      case Kind::Melody: ++result_.melodies; break;
      case Kind::Palette: ++result_.palettes; break;
      case Kind::Mp3: ++result_.mp3; break;
      case Kind::Script: ++result_.scripts; break;
      default: break;
    }
  } else {
    skip("finalize failed /" + name_);
    sink_.abortFile();
  }
  fileOpen_ = false;
}

void RestoreApplier::onArchiveEnd() {
  if (completed_) return;
  if (fileOpen_) fail("truncated archive");
  if (fatal_) {
    result_.ok = false;
    return;
  }
  if (!manifestOk_) {
    result_.ok = false;
    if (result_.error.empty()) result_.error = "no manifest.json";
    return;
  }
  if (!pendingIconOrigins_.empty()) {
    std::string err;
    if (sink_.applyIconOrigins(pendingIconOrigins_, err)) ++result_.iconOrigins;
    else skip("icon origins not applied: " + err);
  }
  sink_.commit();
  result_.ok = true;
  completed_ = true;
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
  w.member("iconOrigins", iconOrigins);
  w.member("melodies", melodies);
  w.member("palettes", palettes);
  w.member("mp3", mp3);
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
