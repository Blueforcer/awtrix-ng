#include "core/api/AssetApi.h"

#include <utility>

#include "core/api/JsonReader.h"
#include "core/api/JsonText.h"
#include "core/api/JsonWriter.h"
#include "core/api/MelodiesApi.h"
#include "core/icons/IconOrigins.h"

namespace awtrix::api::asset {
namespace {

HttpResult success(int status = 200) {
  HttpResult result;
  result.status = status;
  result.body = "{\"ok\":true}";
  return result;
}

bool safePath(const std::string& path) {
  if (path.find("..") != std::string::npos) return false;
  for (unsigned char c : path)
    if (c < 0x20 || c == 0x7f || c == '\\' || c == ':') return false;
  return true;
}

HttpResult invalidPath(bool filename) {
  return errorResult(400, "invalidPath", filename ? "invalid filename" : "invalid path");
}

HttpResult invalidContent(const std::string& path) {
  return errorResult(415, "unsupportedMediaType",
      std::string("expected ") + assets::acceptedFormats(assets::kindFor(path)));
}

void changed(Backend& storage) {
  storage.changed();
}

bool removeFile(Backend& storage, const std::string& path) {
  storage.release(path);
  return storage.remove(path);
}

bool readRename(const std::string& body, std::string& from, std::string& to) {
  if (!isWellFormed(body)) return false;
  JsonReader r(body);
  if (!r.isObject() || !r.enterObject()) return false;
  while (r.nextMember()) {
    std::string* dest = r.keyEquals("from") ? &from : r.keyEquals("to") ? &to : nullptr;
    if (dest && (!r.isString() || !r.appendString(*dest))) return false;
    if (!r.skipValue()) return false;
  }
  return r.ok();
}

std::string iconStem(const std::string& name) { return name.substr(0, name.size() - 4); }

}

HttpResult claimName(const std::string& path,
                     const std::function<bool(const std::string&)>& exists) {
  const std::string namesake = sound::namesakePath(path);
  if (!namesake.empty() && exists && exists(namesake))
    return errorResult(409, "nameTaken", "name taken");
  return success();
}

HttpResult writeMelody(const std::string& name, const std::string& body, Backend& storage) {
  const melodies::PutResult prepared = melodies::prepareWrite(name, body);
  if (!prepared.ok)
    return errorResult(prepared.status, prepared.code.c_str(), prepared.message, prepared.field);
  const std::string path = melodies::pathFor(name);
  HttpResult claimed = claimName(path, [&](const std::string& other) { return storage.exists(other); });
  if (claimed.status != 200) return claimed;
  const bool existed = storage.exists(path);
  if (!storage.write(path, prepared.content))
    return errorResult(507, "insufficientStorage", "write failed");
  changed(storage);
  return success(existed ? 200 : 201);
}

HttpResult deleteMelody(const std::string& name, Backend& storage) {
  // Validates the route parameter itself; nameFromFile is for listings.
  if (!rtttl::validName(name) || !removeFile(storage, melodies::pathFor(name)))
    return errorResult(404, "notFound", "melody not found");
  changed(storage);
  return success();
}

HttpResult deleteFile(const std::string& path, Backend& storage) {
  if (!safePath(path) || !assets::isWritable(path)) return invalidPath(false);
  if (path.rfind("/ICONS/", 0) == 0 && iconorigins::validName(path.substr(7))) {
    // Clears the Hub link first; refuses deletion when that cannot be saved.
    if (!storage.iconOrigins)
      return errorResult(500, "internalError", "icon origins unavailable");
    const auto result = iconorigins::handle(*storage.iconOrigins, "DELETE", {}, path.substr(7));
    if (result.status != 200) return {result.status, "application/json", result.body};
  }
  if (!removeFile(storage, path))
    return errorResult(404, "notFound", "file not found");
  changed(storage);
  return success();
}

HttpResult renameIcon(const std::string& body, Backend& storage) {
  std::string from, to;
  if (!readRename(body, from, to)) return errorResult(400, "invalidJson", "invalid JSON");
  if (!iconorigins::validName(from)) return errorResult(400, "invalidName", "invalid icon name", "from");
  if (!iconorigins::validName(to)) return errorResult(400, "invalidName", "invalid icon name", "to");
  if (from.compare(from.size() - 4, 4, to, to.size() - 4, 4) != 0)
    return errorResult(400, "invalidName", "extension must stay", "to");
  const std::string source = "/ICONS/" + from;
  if (!storage.exists(source)) return errorResult(404, "notFound", "icon not found");
  if (to == from) return success();
  const std::string stem = "/ICONS/" + iconStem(to);
  if (storage.exists(stem + ".gif") || storage.exists(stem + ".jpg"))
    return errorResult(409, "nameTaken", "name taken", "to");
  if (!storage.iconOrigins)
    return errorResult(500, "internalError", "rename unavailable");
  const std::string target = "/ICONS/" + to;
  if (!storage.rename(source, target)) return errorResult(500, "internalError", "rename failed");
  const auto moved = iconorigins::rename(*storage.iconOrigins, from, to);
  if (moved.status != 200) {
    storage.rename(target, source);
    return {moved.status, "application/json", moved.body};
  }
  changed(storage);
  return success();
}

HttpResult deleteMp3(const std::string& name, Backend& storage) {
  const std::string path = sound::mp3PathFor(name);
  if (path.empty()) return invalidMp3Name();
  if (!removeFile(storage, path)) return missingMp3();
  changed(storage);
  return success();
}

HttpResult renameMp3(const std::string& body, Backend& storage) {
  std::string from, to;
  if (!readRename(body, from, to)) return errorResult(400, "invalidJson", "invalid JSON");
  const std::string source = sound::mp3PathFor(from), target = sound::mp3PathFor(to);
  if (source.empty()) return errorResult(400, "invalidName", "invalid name", "from");
  if (target.empty()) return errorResult(400, "invalidName", "invalid name", "to");
  if (!storage.exists(source)) return missingMp3();
  if (to == from) return success();
  if (storage.exists(target) || storage.exists(sound::namesakePath(target)))
    return errorResult(409, "nameTaken", "name taken", "to");
  storage.release(source);
  if (!storage.rename(source, target)) return errorResult(500, "internalError", "rename failed");
  changed(storage);
  return success();
}

HttpResult invalidMp3Name() {
  return errorResult(400, "invalidName",
                "invalid name");
}

HttpResult invalidMp3FileName() {
  return errorResult(400, "invalidName",
      "invalid file name");
}

HttpResult missingMp3() { return errorResult(404, "notFound", "no such MP3"); }

HttpResult listDirectory(const std::string& requested, std::string& normalized) {
  normalized.clear();
  if (!safePath(requested)) return errorResult(400, "invalidPath", "invalid path");
  normalized = requested.empty() || requested.front() != '/' ? "/" + requested : requested;
  return success();
}

UploadTarget prepareUpload(const std::string& directory, const std::string& filename) {
  UploadTarget target;
  if (filename.empty()) {
    target.result = invalidPath(true);
    return target;
  }
  if (filename.front() == '/') {
    target.path = filename;
  } else {
    std::string dir;
    target.result = listDirectory(directory, dir);
    if (target.result.status != 200) return target;
    if (dir.back() != '/') dir += '/';
    target.path = dir + filename;
  }
  if (!safePath(target.path) || !assets::isWritable(target.path) || target.path.back() == '/') {
    target.result = invalidPath(true);
    return target;
  }
  // MP3s are stored flat; a nested path is refused.
  if (!assets::uploadNameOk(target.path) ||
      (assets::kindFor(target.path) == assets::AssetKind::Mp3 &&
       target.path.find('/', sizeof("/MP3/") - 1) != std::string::npos)) {
    target.result = invalidMp3FileName();
    return target;
  }
  target.result = success();
  return target;
}

HttpResult finishUpload(const std::string& path, bool contentOk, bool writeOk) {
  if (!contentOk) return invalidContent(path);
  if (!writeOk) return errorResult(507, "insufficientStorage", "write failed");
  return success();
}

JsonListing::JsonListing(Kind kind, Emit emit) : kind_(kind), emit_(std::move(emit)) {}

void JsonListing::begin() {
  first_ = true;
  grouping_ = false;
  groupOpen_ = false;
  firstGroup_ = true;
  emit_(kind_ == Kind::Melodies ? "{\"melodies\":[" : "{\"files\":[");
}

void JsonListing::entry(std::string json) {
  if (!first_) json.insert(json.begin(), ',');
  first_ = false;
  emit_(json);
}

void JsonListing::file(const std::string& name, uint64_t size, const std::string& sha256) {
  std::string json;
  // A group's header goes out with its first file.
  if (grouping_ && !groupOpen_) {
    if (!firstGroup_) json += ',';
    json += "{\"name\":";
    appendJsonString(json, script_);
    json += ",\"title\":";
    appendJsonString(json, title_);
    json += orphan_ ? ",\"orphan\":true" : ",\"orphan\":false";
    json += ",\"files\":[";
    firstGroup_ = false;
    groupOpen_ = true;
    first_ = true;
  }
  if (!first_) json += ',';
  first_ = false;
  JsonWriter writer(json);
  writer.beginObject();
  writer.member("name", name);
  writer.member("size", size);
  if (!sha256.empty()) writer.member("sha256", sha256);
  writer.endObject();
  emit_(json);
}

void JsonListing::group(const std::string& script, const std::string& title, bool orphan) {
  if (kind_ != Kind::Mp3) return;
  if (!grouping_) {
    emit_("],\"scripts\":[");
    grouping_ = true;
  } else if (groupOpen_) {
    emit_("]}");
    groupOpen_ = false;
  }
  script_ = script;
  title_ = title;
  orphan_ = orphan;
}

void JsonListing::melody(const std::string& filename, const std::string& content, uint32_t size) {
  const std::string name = melodies::nameFromFile(filename);
  if (!name.empty()) entry(melodies::entryJson(name, content, size));
}

// Writes the decimal digits of a uint64_t.
void JsonListing::end(uint64_t usedBytes, uint64_t totalBytes) {
  std::string tail = "]";
  if (kind_ == Kind::Mp3) tail = !grouping_ ? "],\"scripts\":[]" : groupOpen_ ? "]}]" : "]";
  tail += ",\"usedBytes\":";
  appendUnsigned(tail, usedBytes);
  tail += ",\"totalBytes\":";
  appendUnsigned(tail, totalBytes);
  tail += '}';
  emit_(tail);
}

}
