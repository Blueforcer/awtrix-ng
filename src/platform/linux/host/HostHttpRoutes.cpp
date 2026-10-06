#include "platform/linux/host/HostHttpServer.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <condition_variable>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <functional>
#include <mutex>
#include <thread>
#include <vector>

#include "AppConfig.h"
#include "core/AssetPaths.h"
#include "core/ConfigRules.h"
#include "core/CoreEngine.h"
#include "core/SocProfile.h"
#include "core/api/ApiRouter.h"
#include "core/api/JsonStream.h"
#include "core/api/AssetApi.h"
#include "core/api/HttpProtocol.h"
#include "core/api/FilesApi.h"
#include "core/api/DiagnosticsApi.h"
#include "core/api/AssetUploadSession.h"
#include "core/api/RestoreApi.h"
#include "core/api/ScriptSoundsApi.h"
#include "core/api/JsonCoerce.h"
#include "core/api/JsonWriter.h"
#include "core/api/MelodiesApi.h"
#include "core/api/StateJson.h"
#include "core/api/StateReadApi.h"
#include "core/backup/RestoreApplier.h"
#include "core/render/Canvas.h"
#include "core/render/Color.h"
#include "core/script/ScriptConfig.h"
#include "core/script/ScriptHost.h"
#include "core/net/HostName.h"
#include "core/sound/SoundMp3.h"
#include "persistence/DeviceConfig.h"
#include "persistence/CommandPersistence.h"
#include "persistence/FsRestoreSink.h"
#include "persistence/VfsFile.h"
#include "persistence/IconOriginsStore.h"
#include "persistence/SystemConfigApply.h"
#include "persistence/SystemConfigApi.h"
#include "platform/linux/host/HostStore.h"
#include "platform/linux/host/HostPersistence.h"
#include "platform/posix/sha256.h"
#include "system/Log.h"
#include "system/MonotonicClock.h"
#include "platform/linux/host/vendor/httplib.h"

#include "platform/linux/host/HostHttpInternal.h"
#include "platform/linux/host/HostHttpBody.h"

namespace awtrix {
using namespace host_http;

namespace {

namespace stdfs = std::filesystem;

std::string assetEtag(const std::string& bytes) {
  api::Etag tag;
  tag.append(bytes.data(), bytes.size());
  return tag.value();
}

// Streamed in small reads, so a large MP3 is never held whole. size is what was read.
bool sha256File(const std::string& path, std::string& hex, uint64_t& size) {
  std::ifstream in(stdfs::u8path(path), std::ios::binary);
  if (!in) return false;
  sha256_state state;
  sha256_init(&state);
  char buffer[4096];
  size = 0;
  while (in.read(buffer, sizeof(buffer)) || in.gcount() > 0) {
    const auto n = static_cast<std::size_t>(in.gcount());
    sha256_update(&state, buffer, n);
    size += n;
  }
  if (in.bad()) return false;
  uint8_t digest[32];
  sha256_final(&state, digest);
  char text[65];
  sha256_hex(digest, text);
  hex.assign(text, 64);
  return true;
}

class HostFiles final : public api::Files {
 public:
  HostFiles(const api::scriptsounds::InstalledScripts& installed,
            const std::function<void(const std::string&)>& release,
            const std::function<void()>& changed)
      : Files(installed, iconorigins::storage()), release_(release), changed_(changed) {}
  void list(const std::string& directory, const Visit& visit) override {
    if (directory.empty()) return;
    const auto path = host::hostPath(directory);
    if (path.empty()) return;
    std::error_code error;
    for (stdfs::directory_iterator it(stdfs::u8path(path), error), end; !error && it != end; it.increment(error)) {
      const auto status = it->symlink_status(error);
      if (error || (!stdfs::is_directory(status) && !stdfs::is_regular_file(status))) continue;
      const bool directory = stdfs::is_directory(status);
      const auto size = directory ? 0 : it->file_size(error);
      if (error) break;
      const auto name = it->path().filename().u8string();
      visit({name.c_str(), size, directory});
    }
  }
  bool exists(const std::string& path) override { return fs::isFile(path); }
  bool read(const std::string& path, std::string& content) override {
    return host::readFile(host::hostPath(path), content);
  }
  bool write(const std::string& path, const std::string& content) override {
    return host::writeUpload(host::hostPath(path), content);
  }
  bool remove(const std::string& path) override {
    const auto file = host::hostPath(path);
    std::error_code error;
    return fs::isFile(path) && stdfs::remove(stdfs::u8path(file), error) && !error;
  }
  bool rename(const std::string& from, const std::string& to) override {
    const auto source = host::hostPath(from), target = host::hostPath(to);
    if (source.empty() || target.empty()) return false;
    std::error_code error;
    stdfs::rename(stdfs::u8path(source), stdfs::u8path(target), error);
    return !error;
  }
  bool mkdir(const std::string& directory, bool& created) override {
    const auto folder = host::hostPath(directory);
    if (folder.empty()) return false;
    std::error_code error;
    created = stdfs::create_directories(stdfs::u8path(folder), error);
    return !error;
  }
  void prune(const std::string& directory) override {
    const auto folder = host::hostPath(directory);
    std::error_code error;
    if (!folder.empty() && stdfs::is_directory(stdfs::u8path(folder), error) &&
        stdfs::is_empty(stdfs::u8path(folder), error) && !error) stdfs::remove(stdfs::u8path(folder), error);
  }
  void usage(uint64_t& used, uint64_t& total) override {
    std::size_t totalBytes = 0, usedBytes = 0;
    fs::usage(totalBytes, usedBytes);
    used = usedBytes; total = host::storageCapacity(used);
  }
  bool etag(const std::string& path, std::string& value) override {
    if (!fs::isFile(path)) return false;
    const auto size = fs::fileSize(path);
    if (size < 0 || static_cast<uint64_t>(size) > host::kFsTotalBytes) return false;
    std::ifstream input(stdfs::u8path(host::hostPath(path)), std::ios::binary);
    if (!input) return false;
    api::Etag tag;
    char buffer[4096];
    while (input.read(buffer, sizeof(buffer)) || input.gcount() > 0)
      tag.append(buffer, static_cast<std::size_t>(input.gcount()));
    if (input.bad()) return false;
    value = tag.value();
    return true;
  }
  bool sha256(const std::string& path, std::string& digest, uint64_t& size) override {
    const bool ok = sha256File(host::hostPath(path), digest, size);
    if (!ok) logf("http: could not read %s", path.c_str());
    return ok;
  }
  void release(const std::string& path) override { if (release_) release_(path); }
  void changed() override { if (changed_) changed_(); }
 private:
  const std::function<void(const std::string&)>& release_;
  const std::function<void()>& changed_;
};

class HostReply final : public api::Reply {
 public:
  explicit HostReply(httplib::Response& response) : response_(response) {}
  void send(const api::HttpResult& result, bool = false) override {
    response_.status = result.status;
    response_.set_content(result.body, result.contentType);
  }
  void header(const char* name, const std::string& value) override { response_.set_header(name, value); }
  void chunk(const char* data, std::size_t size) override { response_.body.append(data, size); }
  bool sendFile(const std::string& path, const char* contentType) override {
    std::string bytes;
    if (!host::readFile(host::hostPath(path), bytes)) return false;
    response_.set_content(std::move(bytes), contentType);
    return true;
  }
 private:
  httplib::Response& response_;
};

// Buffers each backup entry in memory and only writes it once complete, creating the parent
// directories that the device's flat filesystem never needs.
class HostRestoreSink : public backup::FsRestoreSink {
 public:
  HostRestoreSink(DeviceConfig& live, StateStore* state, std::function<void()> onConfigChanged,
                  std::function<void(DeviceConfig&)> configAccepted)
      : backup::FsRestoreSink(live, state, std::move(onConfigChanged)),
        configAccepted_(std::move(configAccepted)) {}
  void commit() override {
    if (configTouched_ && configAccepted_) configAccepted_(working_);
    backup::FsRestoreSink::commit();
  }
  bool beginFile(const std::string& path, std::string&) override {
    curPath_ = path;
    buf_.clear();
    return true;
  }
  bool writeFile(const uint8_t* data, std::size_t n) override {
    buf_.append(reinterpret_cast<const char*>(data), n);
    return true;
  }
  bool endFile() override {
    std::error_code ec;
    stdfs::create_directories(stdfs::u8path(host::hostPath(curPath_)).parent_path(), ec);
    return host::writeUpload(host::hostPath(curPath_), buf_);
  }
  void abortFile() override { buf_.clear(); }

 private:
  std::function<void(DeviceConfig&)> configAccepted_;
  std::string curPath_;
  std::string buf_;
};

}

api::HttpResult HostHttpServer::Impl::uploadParts(const httplib::Request& req,
    const api::UploadTargetFn& target, api::Files& files) {
  const std::string* content = nullptr;
  api::asset::StreamStorage storage;
  storage.exists = [&](const std::string& path) { return files.exists(path); };
  storage.mkdir = [&](const std::string& directory, bool& created) { return files.mkdir(directory, created); };
  storage.prune = [&](const std::string& directory) { files.prune(directory); };
  // httplib already owns the complete part; validation borrows those bytes until publication.
  storage.begin = [](const std::string&) { return true; };
  storage.write = [](const uint8_t*, std::size_t) { return true; };
  storage.publish = [&](const std::string& path) {
    files.release(path);
    return content && files.write(path, *content);
  };
  storage.changed = [&] { files.changed(); };
  api::asset::UploadSession session;
  session.reset(std::move(storage));
  session.authorize({});
  for (const auto& field : req.files) {
    if (field.second.filename.empty()) continue;
    content = &field.second.content;
    session.start(target(field.second.filename));
    session.append(reinterpret_cast<const uint8_t*>(content->data()), content->size());
    session.end();
  }
  return session.complete();
}

void HostHttpServer::Impl::handleRestore(const httplib::Request& req, const std::string& method,
                                        httplib::Response& res) {
  if (method != "POST") {
    sendError(res, 405, "methodNotAllowed", "allowed: POST");
    return;
  }
  const std::string* data = nullptr;
  for (const auto& part : req.files) {
    if (part.second.filename.empty()) continue;
    data = &part.second.content;
    break;
  }
  if (!data && !req.is_multipart_form_data() && !req.body.empty()) data = &req.body;
  if (!data) {
    sendError(res, 400, "badRequest", "no file received");
    return;
  }
  HostRestoreSink sink(*cfg, &engine->state(), onConfigChanged, options.configAccepted);
  backup::RestoreApplier applier(sink);
  backup::ZipReader reader(applier);
  reader.feed(reinterpret_cast<const uint8_t*>(data->data()), data->size());
  const backup::RestoreResult r = api::finishRestore(reader, applier);
  if (api::restoreChangedAssets(r) && onAssetsChanged) onAssetsChanged();
  if (r.ok)
    logf("host restore: applied wifi=%d system=%d settings=%d apploop=%d icons=%d melodies=%d "
         "palettes=%d scripts=%d",
         r.wifi, r.system, r.settings, r.appLoop, r.icons, r.melodies, r.palettes, r.scripts);
  else
    logf("host restore: rejected - %s", r.error.c_str());
  const auto response = api::restoreResponse(r, host::persistence::hasPending());
  sendJson(res, response.status, response.body);
}

void HostHttpServer::Impl::route(const httplib::Request& req, httplib::Response& res) {
  if (options.routeGuard && !options.routeGuard(req, res)) return;
  const std::string& path = req.path;
  const api::MethodResolution resolved = api::resolveHttpMethod(
      req.method, path,
      req.has_header(api::kMethodOverrideHeader)
          ? req.get_header_value(api::kMethodOverrideHeader)
          : std::string());
  if (resolved.error) {
    sendError(res, 400, "invalidMethodOverride", resolved.error);
    return;
  }
  const std::string& method = resolved.method;

  // A POST here is an upload; deletion needs DELETE.
  if (req.method == "POST" && method != "POST" &&
      api::isUploadRoute(path)) {
    sendError(res, 405, "methodNotAllowed", "allowed: POST");
    return;
  }

  if (!api::acceptsBodyContentType(method, path, req.get_header_value("Content-Type"))) {
    sendError(res, 415, "unsupportedMediaType", "expected application/json");
    return;
  }

  if (path == "/" || path == "/index.html" || path == "/fullscreen") {
    std::string html;
    if (!host::readTrustedFile(webuiFile, html)) {
      res.status = 500;
      res.set_content("webui file not found: " + webuiFile, "text/plain");
      return;
    }
    res.set_header("Cache-Control", "no-cache");
    if (webuiGzip) {
      const std::string etag = assetEtag(html);
      res.set_header("ETag", etag);
      if (req.get_header_value("If-None-Match") == etag) {
        res.status = 304;
        return;
      }
      res.set_header("Content-Encoding", "gzip");
    }
    res.set_content(html, "text/html");
    return;
  }

  const api::scriptsounds::InstalledScripts installed(scripts, scriptSource);
  HostFiles files(installed, releaseFiles, onAssetsChanged);
  HostReply reply(res);
  const api::Request fileRequest{method, path, req.body,
      [&](const char* name, std::string& value) {
        if (!req.has_param(name)) return false;
        value = req.get_param_value(name);
        return true;
      },
      [&](const api::UploadTargetFn& target) { return uploadParts(req, target, files); },
      req.get_header_value("If-None-Match")};
  if (api::routeFiles(fileRequest, reply, files)) return;
  if (serveCommand(req, method, res)) return;
  if (serveState(req, method, res)) return;
  if (api::routeDiagnostics(fileRequest, reply, [this](api::Reply& response) {
        std::string body = "[]";
        const int status = options.wifiScan ? options.wifiScan(body) : 200;
        response.send({status, "application/json", std::move(body)});
      }, logbuf::streamJsonAfter)) return;
  if (api::routeSystemConfig(fileRequest, reply, *cfg, [this] {
        if (options.configAccepted) options.configAccepted(*cfg);
        const bool saved = cfg->save();
        if (onConfigChanged) onConfigChanged();
        return saved;
      })) return;

  if (path == "/api/v1/restore") {
    handleRestore(req, method, res);
    return;
  }

  if (path == "/update") {
    if (options.updateUpload) sendError(res, 405, "methodNotAllowed", "allowed: POST");
    else sendError(res, 501, "notSupported", "firmware updates are unavailable on this platform");
    return;
  }

  if (options.platformHandler && options.platformHandler(req, res)) return;
  if (options.platformRoute) {
    std::string body;
    if (const int status = options.platformRoute(method, path, req.body, body)) {
      sendJson(res, status, body);
      reportError(req, method, status, body);
      return;
    }
  }

  sendError(res, 404, "notFound", "unknown route");
}

// The command routes are shared with the device: api::routeHttp turns the request into a Command
// and decides the response, so this only moves bytes. Divergence here would be a bug.
bool HostHttpServer::Impl::serveCommand(const httplib::Request& req, const std::string& method,
                                       httplib::Response& res) {
  Command cmd;
  api::HttpResult immediate;
  switch (api::routeHttp(method, req.path, std::string(req.body), cmd, immediate)) {
    case api::RouteOutcome::Respond:
      sendJson(res, immediate.status, immediate.body);
      reportError(req, method, immediate.status, immediate.body);
      return true;
    case api::RouteOutcome::Routed: {
      const DispatchResult r = engine->execute(cmd);
      const api::HttpResult out =
          api::commandResponse(*engine, cmd, r, persistence::commandPending(cmd.type));
      if (out.retryAfterSeconds > 0)
        res.set_header("Retry-After", std::to_string(out.retryAfterSeconds));
      sendJson(res, out.status, out.body);
      reportError(req, method, out.status, out.body);
      return true;
    }
    case api::RouteOutcome::NoMatch:
    default:
      return false;
  }
}

void HostHttpServer::Impl::reportError(const httplib::Request& req, const std::string& method,
                                      int status, const std::string& body) {
  if (!onError || status < 400) return;
  const std::string event = api::errorEvent("http", method + " " + req.path, body);
  if (!event.empty()) onError(event);
}

bool HostHttpServer::Impl::serveState(const httplib::Request& req, const std::string& method,
                                     httplib::Response& res) {
  if (method != "GET") return false;
  const api::StateReadContext context{*engine, *screen, capabilitiesJson, scripts,
                                      scriptSource, scriptStore, storedScripts, options.deviceState,
                                      &deviceCapabilities};
  std::string body;
  const auto result = api::readState(method, req.path, context, body);
  if (result.matched) {
    res.status = result.status;
    res.set_content(body, result.contentType);
    return true;
  }
  return false;
}

}
