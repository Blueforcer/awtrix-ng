#include "core/api/ScriptSoundsApi.h"

#include "core/script/ScriptHost.h"
#include "core/script/ScriptMeta.h"
#include "core/sound/SoundMp3.h"

namespace awtrix::api::scriptsounds {
void list(const std::string& script, asset::JsonListing& listing, Backend& storage) {
  const std::string directory = sound::scriptSoundDir(script);
  if (directory.empty()) return;
  storage.eachFile(directory, [&](const std::string& name) {
    if (sound::mp3NameOfFile(name).empty()) return;
    std::string digest;
    uint64_t size = 0;
    if (storage.sha256(directory + "/" + name, digest, size)) listing.file(name, size, digest);
  });
}

namespace {

constexpr std::string_view kPrefix = "/api/v1/apps/script/";
constexpr std::string_view kSounds = "/sounds";

HttpResult invalidScript() {
  return errorResult(400, "invalidName", "invalid name", "name");
}

HttpResult unknownScript() { return errorResult(404, "notFound", "no such script"); }

HttpResult done() {
  HttpResult result;
  result.body = "{\"ok\":true}";
  return result;
}

// The script segment and whatever follows "/sounds"; false for any other path.
bool split(std::string_view path, std::string_view& script, std::string_view& rest) {
  if (path.substr(0, kPrefix.size()) != kPrefix) return false;
  const std::string_view tail = path.substr(kPrefix.size());
  const std::size_t slash = tail.find('/');
  if (slash == std::string_view::npos || tail.substr(slash, kSounds.size()) != kSounds)
    return false;
  rest = tail.substr(slash + kSounds.size());
  if (!rest.empty() && rest.front() != '/') return false;
  script = tail.substr(0, slash);
  return true;
}

}

Route match(std::string_view path) {
  Route route;
  std::string_view script, rest;
  if (!split(path, script, rest)) return route;
  route.matched = true;
  route.script.assign(script.data(), script.size());
  route.item = !rest.empty();
  if (route.item) route.sound.assign(rest.data() + 1, rest.size() - 1);
  return route;
}

bool isUpload(std::string_view path) {
  std::string_view script, rest;
  return split(path, script, rest) && rest.empty();
}

bool InstalledScripts::find(const std::string& name, std::string* title) const {
  if (!isValidAppName(name)) return false;
  if (host_) {
    if (!host_->has(name)) return false;
    if (title) {
      const script::ScriptMeta* meta = host_->metaOf(name);
      *title = meta && !meta->name.empty() ? meta->name : name;
    }
    return true;
  }
  std::string source;
  if (!source_ || !source_(name, source)) return false;
  if (title) {
    const std::string declared = script::parseMeta(source).name;
    *title = declared.empty() ? name : declared;
  }
  return true;
}

HttpResult checkSounds(const std::string& script, const InstalledScripts& scripts, bool hasSounds) {
  if (!isValidAppName(script)) return invalidScript();
  if (!hasSounds && !scripts.find(script)) return unknownScript();
  return HttpResult{};
}

asset::UploadTarget uploadTarget(const std::string& script, const std::string& filename,
                                 const InstalledScripts& scripts) {
  asset::UploadTarget target;
  if (!isValidAppName(script)) {
    target.result = invalidScript();
    return target;
  }
  const std::string path = sound::scriptMp3PathFor(script, sound::mp3NameOfFile(filename));
  if (path.empty()) {
    target.result = asset::invalidMp3FileName();
    return target;
  }
  if (!scripts.find(script)) {
    target.result = unknownScript();
    return target;
  }
  target.path = path;
  return target;
}

HttpResult deleteSound(const std::string& script, const std::string& sound, Backend& storage) {
  if (!isValidAppName(script)) return invalidScript();
  const std::string path = sound::scriptMp3PathFor(script, sound);
  if (path.empty()) return asset::invalidMp3Name();
  storage.release(path);
  if (!storage.remove(path)) return asset::missingMp3();
  storage.prune(sound::scriptSoundDir(script));
  storage.changed();
  return done();
}

// Collects the names before deleting files.
HttpResult deleteSounds(const std::string& script, Backend& storage) {
  if (!isValidAppName(script)) return invalidScript();
  const std::string dir = sound::scriptSoundDir(script);
  std::vector<std::string> files;
  storage.eachFile(dir, [&](const std::string& file) { files.push_back(file); });
  if (!files.empty()) {
    storage.release(dir);
    for (const std::string& file : files)
      storage.remove(dir + "/" + file);
  }
  storage.prune(dir);
  if (!files.empty()) storage.changed();
  return done();
}

}
