#include "sim/SimHttpServer.h"

#include <cmath>
#include <condition_variable>
#include <cstdio>
#include <cstring>
#include <filesystem>
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
#include "core/api/JsonCoerce.h"
#include "core/api/JsonWriter.h"
#include "core/api/SoundsApi.h"
#include "core/api/StateJson.h"
#include "core/backup/RestoreApplier.h"
#include "core/render/Canvas.h"
#include "core/render/Color.h"
#include "core/script/ScriptConfig.h"
#include "core/script/ScriptHost.h"
#include "core/net/HostName.h"
#include "persistence/DeviceConfig.h"
#include "persistence/FsRestoreSink.h"
#include "persistence/SystemConfigApply.h"
#include "sim/SimBoard.h"
#include "sim/SimStore.h"
#include "system/Log.h"
#include "system/MonotonicClock.h"
#include "vendor/httplib.h"

namespace awtrix {

namespace {

namespace stdfs = std::filesystem;

const char* mimeFor(const std::string& path) {
  const size_t dot = path.rfind('.');
  const std::string ext = (dot == std::string::npos) ? "" : path.substr(dot);
  if (ext == ".jpg" || ext == ".jpeg") return "image/jpeg";
  if (ext == ".gif") return "image/gif";
  if (ext == ".png") return "image/png";
  if (ext == ".txt") return "text/plain";
  return "application/octet-stream";
}

bool isServableAsset(const std::string& path) {
  return assets::isServable(path);
}

std::string assetEtag(const std::string& bytes) {
  uint32_t h = 2166136261u;
  for (const char c : bytes) {
    h ^= static_cast<uint8_t>(c);
    h *= 16777619u;
  }
  char tag[40];
  std::snprintf(tag, sizeof(tag), "\"%zx-%lx\"", bytes.size(), static_cast<unsigned long>(h));
  return tag;
}

// Buffers each backup entry in memory and only writes it once complete, creating the parent
// directories that the device's flat filesystem never needs.
class HostRestoreSink : public backup::FsRestoreSink {
 public:
  using backup::FsRestoreSink::FsRestoreSink;
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
    stdfs::create_directories(stdfs::u8path(sim::hostPath(curPath_)).parent_path(), ec);
    return sim::writeFile(sim::hostPath(curPath_), buf_);
  }
  void abortFile() override { buf_.clear(); }

 private:
  std::string curPath_;
  std::string buf_;
};

// Every fact below is invented. The host has no radio, no battery gauge and no meaningful heap
// figures, but the web UI expects the same shape the device sends.
std::string simDeviceStateJson(CoreEngine& engine, const std::string& uid,
                               const std::string& hostname, bool scriptingRunning) {
  DeviceFacts facts;
  facts.boardType = "simulator";
  facts.soc = pins::activeProfile().id;
  facts.ipAddress = "127.0.0.1";
  facts.hostname = hostname;
  facts.wifiRssi = -40;
  facts.uptimeSeconds = static_cast<long>(monotonicMs() / 1000);
  facts.freeHeapBytes = 4194304;
  facts.minFreeHeapBytes = 4194304;
  facts.resetReason = "unknown";
  facts.hasBattery = true;
  facts.hasLightSensor = true;
  facts.hasTemperature = true;
  facts.hasHumidity = true;
  facts.hasPressure = false;
  facts.scriptingRunning = scriptingRunning;
  return buildDeviceJson(engine, uid, facts);
}

void sendJson(httplib::Response& res, int status, const std::string& body) {
  res.status = status;
  res.set_content(body, "application/json");
}

void sendError(httplib::Response& res, int status, const char* code, const char* message) {
  sendJson(res, status, api::errorJson(code, message));
}


bool bodyObject(httplib::Response& res, const std::string& body) {
  if (!api::isWellFormed(body)) {
    sendError(res, 400, "invalidJson", "request body is not valid JSON");
    return false;
  }
  if (!api::JsonReader(body).isObject()) {
    sendError(res, 400, "invalidJson", "request body must be a JSON object");
    return false;
  }
  return true;
}

bool onlyKnownFields(httplib::Response& res, const std::string& body,
                     std::initializer_list<const char*> allowed) {
  api::JsonReader r{std::string_view(body)};
  if (!r.enterObject()) return true;
  while (r.nextMember()) {
    const std::string key(r.key());
    bool known = false;
    for (const char* a : allowed)
      if (key == a) { known = true; break; }
    if (!known) {
      std::string msg = "unknown field '" + key + "' (";
      bool first = true;
      for (const char* a : allowed) {
        if (!first) msg += '|';
        msg += a;
        first = false;
      }
      msg += ')';
      sendError(res, 400, "unknownField", msg.c_str());
      return false;
    }
    if (!r.skipValue()) break;
  }
  return true;
}

}

struct SimHttpServer::Impl {
  httplib::Server svr;
  std::thread listener;
  uint16_t port = 8080;

  std::mutex m;
  std::condition_variable cv;
  std::vector<std::function<void()>> jobs;
  bool stopping = false;

  CoreEngine* engine = nullptr;
  SimBoard* board = nullptr;
  Canvas* screen = nullptr;
  DeviceConfig* cfg = nullptr;
  std::string uid;
  std::string webuiFile;
  std::string capabilitiesJson = "{}";
  std::function<void()> onConfigChanged;
  std::function<void()> onAssetsChanged;
  const script::ScriptHost* scripts = nullptr;
  SimHttpServer::ScriptSourceFn scriptSource;
  SimHttpServer::ScriptSourceFn scriptStore;
  SimHttpServer::StoredScriptsFn storedScripts;

  // httplib answers on its own thread, but the engine is single-threaded. Every handler is queued
  // here and blocks until tick() has run it on the main loop, so no route ever needs a lock.
  void runOnLoop(const std::function<void()>& fn) {
    std::unique_lock<std::mutex> lk(m);
    if (stopping) return;
    bool done = false;
    jobs.push_back([&, fn] {
      fn();
      std::lock_guard<std::mutex> g(m);
      done = true;
      cv.notify_all();
    });
    cv.wait(lk, [&] { return done || stopping; });
  }

  void route(const httplib::Request& req, httplib::Response& res);
  bool serveCommand(const httplib::Request& req, const std::string& method, httplib::Response& res);
  bool serveState(const httplib::Request& req, const std::string& method, httplib::Response& res);
  bool serveSystem(const httplib::Request& req, const std::string& method, httplib::Response& res);
  std::string systemJson(bool withSecrets = false) const;
  void handleFiles(const httplib::Request& req, const std::string& method, httplib::Response& res);
  void handleSounds(const httplib::Request& req, const std::string& method, httplib::Response& res);
  void handleRestore(const httplib::Request& req, const std::string& method, httplib::Response& res);
  void handleSim(const httplib::Request& req, const std::string& method, httplib::Response& res);
};

std::string SimHttpServer::Impl::systemJson(bool withSecrets) const {
  std::string out;
  out.reserve(1536);
  api::JsonWriter w(out);
  w.beginObject();
  cfg->write(w, withSecrets);
  w.endObject();
  return out;
}

void SimHttpServer::Impl::handleRestore(const httplib::Request& req, const std::string& method,
                                        httplib::Response& res) {
  if (method != "POST") {
    sendError(res, 405, "methodNotAllowed", "allowed method(s): POST");
    return;
  }
  HostRestoreSink sink(*cfg, &engine->state(), onConfigChanged);
  backup::RestoreApplier applier(sink);
  backup::ZipReader reader(applier);
  const std::string* data = nullptr;
  if (!req.files.empty()) data = &req.files.begin()->second.content;
  else if (!req.body.empty()) data = &req.body;
  if (data) reader.feed(reinterpret_cast<const uint8_t*>(data->data()), data->size());
  reader.finish();
  const backup::RestoreResult r = applier.result();
  if ((r.icons || r.melodies || r.palettes) && onAssetsChanged) onAssetsChanged();
  if (r.ok)
    logf("sim restore: applied wifi=%d system=%d settings=%d apploop=%d icons=%d melodies=%d "
         "palettes=%d scripts=%d",
         r.wifi, r.system, r.settings, r.appLoop, r.icons, r.melodies, r.palettes, r.scripts);
  else
    logf("sim restore: rejected - %s", r.error.c_str());
  sendJson(res, r.ok ? 200 : 400, r.toJson());
}

void SimHttpServer::Impl::handleSounds(const httplib::Request& req, const std::string& method,
                                       httplib::Response& res) {
  const std::string path = req.path;

  if (path == "/api/v1/sounds") {
    if (method != "GET") {
      sendError(res, 405, "methodNotAllowed", "allowed method(s): GET");
      return;
    }
    std::string out = "{\"melodies\":[";
    std::error_code ec;
    bool first = true;
    for (stdfs::directory_iterator it(stdfs::u8path(sim::hostPath("/MELODIES")), ec), end;
         !ec && it != end; ++it) {
      if (!it->is_regular_file()) continue;
      const std::string name = api::sounds::nameFromFile(it->path().filename().u8string());
      if (name.empty()) continue;
      std::string content;
      if (!sim::readFile(it->path().u8string(), content)) continue;
      if (!first) out += ',';
      first = false;
      out += api::sounds::entryJson(name, content, static_cast<uint32_t>(content.size()));
    }
    // Usage is measured for real on disk, but there is no flash partition to report a size for, so
    // the total is a plausible 8 MB stand-in for the device's SPIFFS area.
    uint64_t used = 0;
    for (stdfs::recursive_directory_iterator it(stdfs::u8path(sim::dataDir()), ec), end;
         !ec && it != end; ++it)
      if (it->is_regular_file()) used += it->file_size(ec);
    out += "],\"usedBytes\":" + std::to_string(used);
    out += ",\"totalBytes\":" + std::to_string(8u * 1024u * 1024u) + "}";
    sendJson(res, 200, out);
    return;
  }

  const std::string name = path.substr(sizeof("/api/v1/sounds/") - 1);
  const std::string file = sim::hostPath(api::sounds::pathFor(name));

  if (method == "PUT") {
    const api::sounds::PutResult r = api::sounds::prepareWrite(name, req.body);
    if (!r.ok) {
      sendJson(res, r.status, api::errorJson(r.code.c_str(), r.message, r.field));
      return;
    }
    std::error_code ec;
    const bool existed = stdfs::exists(stdfs::u8path(file), ec);
    stdfs::create_directories(stdfs::u8path(file).parent_path(), ec);
    if (!sim::writeFile(file, r.content)) {
      sendError(res, 507, "insufficientStorage", "could not write the melody");
      return;
    }
    logf("sim sounds: saved %s (%u B)", name.c_str(), static_cast<unsigned>(r.content.size()));
    if (onAssetsChanged) onAssetsChanged();
    sendJson(res, existed ? 200 : 201, "{\"ok\":true}");
    return;
  }

  if (method == "DELETE") {
    std::error_code ec;
    if (!api::sounds::nameFromFile(name + ".txt").empty() &&
        stdfs::remove(stdfs::u8path(file), ec)) {
      if (onAssetsChanged) onAssetsChanged();
      sendJson(res, 200, "{\"ok\":true}");
    } else {
      sendError(res, 404, "notFound", "melody not found");
    }
    return;
  }

  sendError(res, 405, "methodNotAllowed", "allowed method(s): PUT, DELETE");
}

void SimHttpServer::Impl::handleFiles(const httplib::Request& req, const std::string& method,
                                      httplib::Response& res) {
  if (method == "GET") {
    std::string dir = req.has_param("dir") ? req.get_param_value("dir") : "/ICONS";
    if (dir.empty() || dir[0] != '/') dir = "/" + dir;
    if (dir.find("..") != std::string::npos) {
      sendError(res, 400, "invalidPath", "path traversal rejected");
      return;
    }
    std::string out = "{\"files\":[";
    std::error_code ec;
    bool first = true;
    for (stdfs::directory_iterator it(stdfs::u8path(sim::hostPath(dir)), ec), end;
         !ec && it != end; ++it) {
      if (!it->is_regular_file()) continue;
      if (!first) out += ',';
      first = false;
      api::JsonWriter ew(out);
      ew.beginObject();
      ew.member("name", it->path().filename().u8string());
      ew.member("size", static_cast<uint32_t>(it->file_size(ec)));
      ew.endObject();
    }
    uint64_t used = 0;
    for (stdfs::recursive_directory_iterator it(stdfs::u8path(sim::dataDir()), ec), end;
         !ec && it != end; ++it)
      if (it->is_regular_file()) used += it->file_size(ec);
    out += "],\"usedBytes\":" + std::to_string(used);
    out += ",\"totalBytes\":" + std::to_string(8u * 1024u * 1024u) + "}";
    sendJson(res, 200, out);
    return;
  }
  if (method == "POST") {
    std::string dir = req.has_param("dir") ? req.get_param_value("dir") : "/ICONS";
    if (dir.empty() || dir[0] != '/') dir = "/" + dir;
    if (dir.find("..") != std::string::npos) {
      sendError(res, 400, "invalidPath", "path traversal rejected");
      return;
    }
    for (const auto& kv : req.files) {
      const auto& f = kv.second;
      if (f.filename.empty()) continue;
      std::string target = f.filename;
      if (target[0] != '/') target = dir + "/" + target;
      if (!assets::isWritable(target)) {
        sendError(res, 400, "invalidPath",
                  "filename must be under /ICONS, /MELODIES or /PALETTES and contain no '..'");
        return;
      }
      const assets::AssetKind kind = assets::kindFor(target);
      if (!assets::contentLooksValid(kind, reinterpret_cast<const unsigned char*>(f.content.data()),
                                     static_cast<unsigned>(f.content.size()))) {
        const std::string msg =
            std::string("file content does not match the target folder; expected ") +
            assets::acceptedFormats(kind);
        sendError(res, 415, "unsupportedMediaType", msg.c_str());
        return;
      }
      std::error_code ec;
      stdfs::create_directories(stdfs::u8path(sim::hostPath(target)).parent_path(), ec);
      sim::writeFile(sim::hostPath(target), f.content);
      logf("sim files: uploaded %s (%u B)", target.c_str(),
           static_cast<unsigned>(f.content.size()));
    }
    if (onAssetsChanged) onAssetsChanged();
    sendJson(res, 200, "{\"ok\":true}");
    return;
  }
  if (method == "DELETE") {
    const std::string fn = req.has_param("path") ? req.get_param_value("path") : "";
    if (!assets::isWritable(fn)) {
      sendError(res, 400, "invalidPath",
                "path must be under /ICONS, /MELODIES or /PALETTES and contain no '..'");
      return;
    }
    std::error_code ec;
    const bool ok = stdfs::remove(stdfs::u8path(sim::hostPath(fn)), ec) && !ec;
    if (ok) {
      if (onAssetsChanged) onAssetsChanged();
      sendJson(res, 200, "{\"ok\":true}");
    } else {
      sendError(res, 404, "notFound", "file not found");
    }
    return;
  }
  sendError(res, 405, "methodNotAllowed", "allowed method(s): GET, POST, DELETE");
}

// The /sim namespace has no counterpart on the device: it is how tests and the web UI press the
// fake buttons and push sensor values into SimBoard. Unknown fields are rejected on purpose.
void SimHttpServer::Impl::handleSim(const httplib::Request& req, const std::string& method,
                                    httplib::Response& res) {
  const std::string& path = req.path;
  if (method == "POST" && path.rfind("/sim/button/", 0) == 0) {
    const std::string btn = path.substr(12);
    if (btn != "left" && btn != "select" && btn != "right") {
      sendError(res, 404, "notFound", "unknown button (left|select|right)");
      return;
    }
    long durationMs = 80;
    if (!req.body.empty()) {
      if (!bodyObject(res, req.body)) return;
      if (!onlyKnownFields(res, req.body, {"durationMs"})) return;
      const api::JsonReader at = api::memberValue(api::JsonReader(req.body), "durationMs");
      if (at.type() != api::JsonReader::Type::Invalid) {
        long long ms = 0;
        if (!at.isNumber() || !at.isInteger() || !at.asLong(ms)) {
          sendError(res, 400, "invalidField", "durationMs must be a whole number of ms");
          return;
        }
        durationMs = static_cast<long>(ms);
        if (durationMs < 40) {
          sendError(res, 400, "invalidField", "durationMs must be >= 40 (35 ms debounce)");
          return;
        }
      }
    }
    const int64_t until = board->now() + durationMs;
    if (btn == "left") board->leftUntilMs = until;
    else if (btn == "select") board->selectUntilMs = until;
    else board->rightUntilMs = until;
    sendJson(res, 200, "{\"ok\":true}");
    return;
  }
  if ((method == "PUT" || method == "PATCH") && path == "/sim/sensors") {
    if (!bodyObject(res, req.body)) return;
    if (!onlyKnownFields(res, req.body,
                         {"temperature", "humidity", "ldrRaw", "batteryPinMillivolts"}))
      return;
    SimSensors& s = board->simSensors();
    api::JsonReader r{std::string_view(req.body)};
    if (r.enterObject()) {
      while (r.nextMember()) {
        if (r.keyEquals("temperature")) s.temperatureC = api::coerceFloat(r);
        else if (r.keyEquals("humidity")) s.humidity = api::coerceFloat(r);
        else if (r.keyEquals("ldrRaw")) board->ldrRaw = api::coerceInt<int>(r);
        else if (r.keyEquals("batteryPinMillivolts"))
          board->batteryPinMillivolts = api::coerceInt<int>(r);
        if (!r.skipValue()) break;
      }
    }
    sendJson(res, 200, "{\"ok\":true}");
    return;
  }
  if (method == "GET" && path == "/sim") {
    sendJson(res, 200,
             "{\"buttons\":\"POST /sim/button/{left|select|right} "
             "(optional body {\\\"durationMs\\\":80}, >= 40)\","
             "\"sensors\":\"PUT /sim/sensors "
             "{\\\"temperature\\\":21.5,\\\"humidity\\\":42,\\\"ldrRaw\\\":1200,"
             "\\\"batteryPinMillivolts\\\":2290}\","
             "\"unknownFields\":\"rejected with 400 -- both routes\"}");
    return;
  }
  sendError(res, 404, "notFound", "unknown /sim route");
}

void SimHttpServer::Impl::route(const httplib::Request& req, httplib::Response& res) {
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
  const bool get = (method == "GET");

  // Mirrors the device's Content-Type gate so a client that works against the sim works there too.
  // Raw-body writes (script sources, melodies) are exempt because they are not JSON.
  if ((method == "PUT" || method == "PATCH") && req.has_header("Content-Type") &&
      !awtrix::api::isRawBodyWrite(method, path)) {
    const std::string ct = req.get_header_value("Content-Type");
    if (!ct.empty() && ct.rfind("application/json", 0) != 0) {
      sendError(res, 415, "unsupportedMediaType", "Content-Type must be application/json");
      return;
    }
  }

  if (path == "/" || path == "/index.html") {
    std::string html;
    if (!sim::readFile(webuiFile, html)) {
      res.status = 500;
      res.set_content("webui file not found: " + webuiFile, "text/plain");
      return;
    }
    res.set_header("Cache-Control", "no-cache");
    res.set_content(html, "text/html");
    return;
  }

  if (path == "/sim" || path.rfind("/sim/", 0) == 0) {
    handleSim(req, method, res);
    return;
  }

  if (get && (isServableAsset(path) || assets::isBackupReadable(path))) {
    std::string bytes;
    if (!sim::readFile(sim::hostPath(path), bytes)) {
      sendError(res, 404, "notFound", "file not found");
      return;
    }
    const std::string etag = assetEtag(bytes);
    res.set_header("ETag", etag);
    res.set_header("Cache-Control", "no-cache");
    if (req.get_header_value("If-None-Match") == etag) {
      res.status = 304;
      return;
    }
    res.set_content(bytes, mimeFor(path));
    return;
  }

  if (serveCommand(req, method, res)) return;
  if (serveState(req, method, res)) return;
  if (serveSystem(req, method, res)) return;

  if (path == "/api/v1/sounds" || path.rfind("/api/v1/sounds/", 0) == 0) {
    handleSounds(req, method, res);
    return;
  }

  if (path == "/api/v1/files") {
    handleFiles(req, method, res);
    return;
  }

  if (path == "/api/v1/restore") {
    handleRestore(req, method, res);
    return;
  }

  if (path == "/update") {
    sendError(res, 501, "notSupported", "firmware OTA does not apply to the simulator");
    return;
  }

  sendError(res, 404, "notFound", "unknown route");
}

// The command routes are shared with the device: api::routeHttp turns the request into a Command
// and decides the response, so this only moves bytes. Divergence here would be a bug.
bool SimHttpServer::Impl::serveCommand(const httplib::Request& req, const std::string& method,
                                       httplib::Response& res) {
  Command cmd;
  api::HttpResult immediate;
  switch (api::routeHttp(method, req.path, std::string(req.body), cmd, immediate)) {
    case api::RouteOutcome::Respond:
      sendJson(res, immediate.status, immediate.body);
      return true;
    case api::RouteOutcome::Routed: {
      const DispatchResult r = engine->execute(cmd);
      if (cmd.type == CommandType::SetSettings && r == DispatchResult::Ok) {
        sendJson(res, 200, buildSettingsJson(*engine));
        return true;
      }
      const api::HttpResult out = api::httpResponse(cmd, r, engine->lastDetail());
      if (out.retryAfterSeconds > 0)
        res.set_header("Retry-After", std::to_string(out.retryAfterSeconds));
      sendJson(res, out.status, out.body);
      return true;
    }
    case api::RouteOutcome::NoMatch:
    default:
      return false;
  }
}

bool SimHttpServer::Impl::serveState(const httplib::Request& req, const std::string& method,
                                     httplib::Response& res) {
  if (method != "GET") return false;
  const std::string& path = req.path;

  if (path == "/api/v1/settings") {
    sendJson(res, 200, buildSettingsJson(*engine));
    return true;
  }
  if (path == "/api/v1/device") {
    sendJson(res, 200, simDeviceStateJson(*engine, uid,
                                          net::effectiveHostname(cfg->hostname, uid),
                                          scripts != nullptr));
    return true;
  }
  if (path == "/api/v1/display") {
    sendJson(res, 200, buildDisplayJson(*engine));
    return true;
  }
  if (path == "/api/v1/display/screen") {
    sendJson(res, 200, buildScreenJson(*screen));
    return true;
  }
  if (path == "/api/v1/radio") {
    sendJson(res, 200, buildRadioJson(*engine));
    return true;
  }
  if (path == "/api/v1/apps") {
    if (scripts || !storedScripts) {
      sendJson(res, 200, buildAppsJson(*engine, scripts));
    } else {
      const std::vector<script::StoredScript> stored = storedScripts();
      std::string out;
      appendAppsJson(out, *engine, nullptr, &stored);
      sendJson(res, 200, out);
    }
    return true;
  }
  if (path == "/api/v1/capabilities") {
    sendJson(res, 200, capabilitiesJson);
    return true;
  }
  if (path == "/api/v1/version") {
    sendJson(res, 200, std::string("{\"version\":\"") + AWTRIX_NG_VERSION + "\"}");
    return true;
  }
  if (path == "/version") {
    res.set_content(AWTRIX_NG_VERSION, "text/plain");
    return true;
  }
  if (path == "/api/v1/system/wifi-scan") {
    sendJson(res, 200, "[]");
    return true;
  }
  if (path == "/api/v1/logs") {
    const uint32_t after = req.has_param("after")
                               ? strtoul(req.get_param_value("after").c_str(), nullptr, 10)
                               : 0;
    sendJson(res, 200, logbuf::jsonAfter(after));
    return true;
  }
  if (path == "/api/v1/scripts/shared") {
    if (!scripts) {
      sendError(res, 503, "unavailable", "scripting is not available");
      return true;
    }
    sendJson(res, 200, buildSharedStateJson(scripts->sharedSnapshot()));
    return true;
  }
  if (path.rfind("/api/v1/apps/script/", 0) == 0) {
    const std::string name = path.substr(std::string("/api/v1/apps/script/").size());
    if (!scriptSource) {
      sendError(res, 503, "unavailable", "scripting is not available");
      return true;
    }
    if (!api::isValidAppName(name)) {
      sendJson(res, 400,
               api::errorJson("invalidName", "name must match [A-Za-z0-9_-]{1,32}", "name"));
      return true;
    }
    std::string source;
    if (!scriptSource(name, source)) {
      sendError(res, 404, "notFound", "no such script");
      return true;
    }
    res.status = 200;
    res.set_content(source, "text/plain");
    return true;
  }

  {
    const std::string name = api::configAppName(path);
    if (!name.empty()) {
      std::string out;
      sendJson(res, script::configResponse(name, scriptSource, scriptStore, out), out);
      return true;
    }
  }
  return false;
}

bool SimHttpServer::Impl::serveSystem(const httplib::Request& req, const std::string& method,
                                      httplib::Response& res) {
  if (req.path != "/api/v1/system") return false;
  if (method == "GET") {
    sendJson(res, 200, systemJson(req.has_param("secrets")));
    return true;
  }
  if (method == "PUT") {
    if (!api::isWellFormed(req.body)) {
      sendError(res, 400, "invalidJson", "request body is not valid JSON");
      return true;
    }
    DeviceConfig merged = *cfg;
    sysconfig::ApplyError ae;
    int applied = 0;
    if (!sysconfig::apply(merged, api::JsonReader(req.body), applied, ae)) {
      sendJson(res, ae.status, api::errorJson(ae.code.c_str(), ae.message, ae.field));
      return true;
    }
    *cfg = merged;
    cfg->save();
    logf("sim config: %d field(s) saved", applied);
    if (onConfigChanged) onConfigChanged();
    sendJson(res, 200, systemJson());
    return true;
  }
  sendError(res, 405, "methodNotAllowed", "allowed method(s): GET, PUT");
  return true;
}

SimHttpServer::SimHttpServer() : impl_(new Impl) {}

SimHttpServer::~SimHttpServer() { stop(); }

void SimHttpServer::setCapabilitiesJson(std::string j) { impl_->capabilitiesJson = std::move(j); }
void SimHttpServer::setOnConfigChanged(std::function<void()> cb) {
  impl_->onConfigChanged = std::move(cb);
}
void SimHttpServer::setOnAssetsChanged(std::function<void()> cb) {
  impl_->onAssetsChanged = std::move(cb);
}
void SimHttpServer::setScripts(const script::ScriptHost* host, ScriptSourceFn readSource,
                               ScriptSourceFn readStore, StoredScriptsFn stored) {
  impl_->scripts = host;
  impl_->scriptSource = std::move(readSource);
  impl_->scriptStore = std::move(readStore);
  impl_->storedScripts = std::move(stored);
}

bool SimHttpServer::begin(uint16_t port, CoreEngine& engine, SimBoard& board, Canvas& screen,
                          const std::string& uid, DeviceConfig& cfg,
                          const std::string& webuiFile) {
  Impl* im = impl_.get();
  im->engine = &engine;
  im->board = &board;
  im->screen = &screen;
  im->cfg = &cfg;
  im->uid = uid;
  im->webuiFile = webuiFile;
  im->port = port;

  im->svr.set_default_headers({{"Access-Control-Allow-Origin", "*"}});

  const auto marshal = [im](const httplib::Request& req, httplib::Response& res) {
    im->runOnLoop([&] { im->route(req, res); });
  };
  im->svr.Get(".*", marshal);
  im->svr.Post(".*", marshal);
  im->svr.Put(".*", marshal);
  im->svr.Patch(".*", marshal);
  im->svr.Delete(".*", marshal);
  im->svr.Options(".*", [](const httplib::Request&, httplib::Response& res) {
    res.status = 204;
    res.set_header("Access-Control-Allow-Methods", "GET, POST, PUT, PATCH, DELETE, OPTIONS");
    res.set_header("Access-Control-Allow-Headers",
                   std::string("Content-Type, Authorization, ") + api::kMethodOverrideHeader);
    res.set_header("Access-Control-Allow-Private-Network", "true");
    res.set_header("Access-Control-Max-Age", "600");
  });

  if (!im->svr.bind_to_port("0.0.0.0", port)) {
    logf("sim http: cannot bind port %u", static_cast<unsigned>(port));
    return false;
  }
  im->listener = std::thread([im] { im->svr.listen_after_bind(); });
  return true;
}

void SimHttpServer::tick() {
  std::vector<std::function<void()>> pending;
  {
    std::lock_guard<std::mutex> g(impl_->m);
    pending.swap(impl_->jobs);
  }
  for (auto& job : pending) job();
}

void SimHttpServer::stop() {
  if (!impl_) return;
  // The stopping flag has to be raised first: it releases any handler still blocked in runOnLoop,
  // which would otherwise wait forever for a tick() that is never coming and deadlock the join.
  {
    std::lock_guard<std::mutex> g(impl_->m);
    impl_->stopping = true;
    impl_->cv.notify_all();
  }
  impl_->svr.stop();
  if (impl_->listener.joinable()) impl_->listener.join();
}

}
