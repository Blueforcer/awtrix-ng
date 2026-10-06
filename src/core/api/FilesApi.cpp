#include "core/api/FilesApi.h"

#include "core/AssetPaths.h"
#include "core/api/MelodiesApi.h"
#include "core/sound/SoundMp3.h"

namespace awtrix::api {

void Files::eachFile(const std::string& directory, const scriptsounds::Backend::Visit& visit) {
  list(directory, [&](const Entry& entry) { if (!entry.directory) visit(entry.name); });
}

asset::UploadTarget uploadTarget(const Request& request, const std::string& filename,
                                 const scriptsounds::InstalledScripts& installed) {
  const auto script = scriptsounds::match(request.path);
  if (script.matched && !script.item) return scriptsounds::uploadTarget(script.script, filename, installed);
  if (request.path == "/api/v1/audio/mp3") return asset::prepareUpload("/MP3", filename);
  if (request.path == "/api/v1/files") return asset::prepareUpload(request.parameter("dir", "/ICONS"), filename);
  return {{}, errorResult(400, "invalidPath", "invalid path")};
}

namespace {
void methodNotAllowed(Reply& reply, const char* allowed) {
  reply.send(errorResult(405, "methodNotAllowed", allowed));
}

void listing(Reply& reply, Files& files, asset::JsonListing::Kind kind, const std::string& directory,
             const std::string& script = {}) {
  reply.send({200, "application/json", {}}, true);
  asset::JsonListing output(kind, [&](const std::string& chunk) { reply.chunk(chunk.data(), chunk.size()); });
  output.begin();
  if (!script.empty()) {
    scriptsounds::list(script, output, files);
  } else {
    files.list(directory, [&](const Files::Entry& entry) {
      if (entry.directory) return;
      if (kind == asset::JsonListing::Kind::Melodies) {
        if (melodies::nameFromFile(entry.name).empty()) return;
        std::string content;
        if (files.read(directory + "/" + entry.name, content))
          output.melody(entry.name, content, static_cast<uint32_t>(content.size()));
      } else output.file(entry.name, entry.size);
    });
    if (kind == asset::JsonListing::Kind::Mp3) {
      files.list("/SCRIPTS", [&](const Files::Entry& entry) {
        if (!entry.directory) return;
        const std::string name(entry.name), sounds = sound::scriptSoundDir(name);
        if (sounds.empty()) return;
        std::string title;
        const bool orphan = !files.installed.find(name, &title);
        output.group(name, orphan ? name : title, orphan);
        files.list(sounds, [&](const Files::Entry& file) {
          if (!file.directory && !sound::mp3NameOfFile(file.name).empty()) output.file(file.name, file.size);
        });
      });
    }
  }
  uint64_t used = 0, total = 0;
  files.usage(used, total);
  output.end(used, total);
  reply.chunk("", 0);
}

void upload(const Request& request, Reply& reply, Files& files) {
  if (!request.upload) { reply.send(errorResult(400, "badRequest", "no file received")); return; }
  reply.send(request.upload([&](const std::string& filename) {
    return uploadTarget(request, filename, files.installed);
  }));
}

bool scriptSounds(const Request& request, Reply& reply, Files& files) {
  const auto route = scriptsounds::match(request.path);
  if (!route.matched) return false;
  if (route.item) {
    if (request.method != "DELETE") methodNotAllowed(reply, "allowed: DELETE");
    else reply.send(scriptsounds::deleteSound(route.script, route.sound, files));
  } else if (request.method == "DELETE") {
    reply.send(scriptsounds::deleteSounds(route.script, files));
  } else if (request.method == "POST") {
    upload(request, reply, files);
  } else if (request.method == "GET") {
    auto checked = scriptsounds::checkSounds(route.script, files.installed, false);
    if (checked.status == 404) {
      bool hasSounds = false;
      files.list(sound::scriptSoundDir(route.script), [&](const Files::Entry& file) {
        if (!file.directory && !sound::mp3NameOfFile(file.name).empty()) hasSounds = true;
      });
      if (hasSounds) checked = scriptsounds::checkSounds(route.script, files.installed, true);
    }
    if (checked.status != 200) reply.send(checked);
    else listing(reply, files, asset::JsonListing::Kind::Files, {}, route.script);
  } else methodNotAllowed(reply, "allowed: GET, POST, DELETE");
  return true;
}
}

bool routeFiles(const Request& request, Reply& reply, Files& files) {
  const auto& path = request.path;
  const auto& method = request.method;
  if (method == "GET" && (assets::isServable(path) || assets::isBackupReadable(path))) {
    std::string tag;
    if (!files.etag(path, tag)) { reply.send(errorResult(404, "notFound", "file not found")); return true; }
    reply.header("ETag", tag);
    reply.header("Cache-Control", "no-cache");
    if (request.ifNoneMatch == tag) reply.send({304, "text/plain", {}});
    else if (!reply.sendFile(path, assets::mimeType(path))) reply.send(errorResult(404, "notFound", "file not found"));
    return true;
  }
  if (scriptSounds(request, reply, files)) return true;
  if (path == "/api/v1/audio/melodies") {
    if (method == "GET") listing(reply, files, asset::JsonListing::Kind::Melodies, "/MELODIES");
    else methodNotAllowed(reply, "allowed: GET");
    return true;
  }
  if (path.rfind("/api/v1/audio/melodies/", 0) == 0) {
    const auto name = path.substr(sizeof("/api/v1/audio/melodies/") - 1);
    if (method == "PUT") reply.send(asset::writeMelody(name, request.body, files));
    else if (method == "DELETE") reply.send(asset::deleteMelody(name, files));
    else methodNotAllowed(reply, "allowed: PUT, DELETE");
    return true;
  }
  if (path == "/api/v1/audio/mp3/rename" && method == "POST") {
    reply.send(asset::renameMp3(request.body, files));
    return true;
  }
  if (path.rfind("/api/v1/audio/mp3/", 0) == 0) {
    const auto name = path.substr(sizeof("/api/v1/audio/mp3/") - 1);
    if (sound::mp3PathFor(name).empty()) reply.send(asset::invalidMp3Name());
    else if (method == "DELETE") reply.send(asset::deleteMp3(name, files));
    else methodNotAllowed(reply, "allowed: DELETE");
    return true;
  }
  if (path == "/api/v1/icons/rename") {
    if (method == "POST") reply.send(asset::renameIcon(request.body, files));
    else methodNotAllowed(reply, "allowed: POST");
    return true;
  }
  if (path == "/api/v1/icons/origins") {
    const auto result = iconorigins::handle(files.origins, method, request.body, request.parameter("name"));
    reply.header("Cache-Control", "no-store");
    reply.send({result.status, "application/json", result.body});
    return true;
  }
  const bool mp3 = path == "/api/v1/audio/mp3";
  if (!mp3 && path != "/api/v1/files") return false;
  if (method == "GET") {
    std::string directory;
    const auto result = asset::listDirectory(mp3 ? "/MP3" : request.parameter("dir", "/ICONS"), directory);
    if (result.status != 200) reply.send(result);
    else listing(reply, files, mp3 ? asset::JsonListing::Kind::Mp3 : asset::JsonListing::Kind::Files, directory);
  } else if (method == "POST") upload(request, reply, files);
  else if (method == "DELETE" && !mp3) {
    reply.send(asset::deleteFile(request.parameter("path"), files));
  } else methodNotAllowed(reply, mp3 ? "allowed: GET, POST" : "allowed: GET, POST, DELETE");
  return true;
}

}
