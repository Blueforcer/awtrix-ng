#include "transport/http/HttpApiServer.h"

#include <LittleFS.h>
#include <Update.h>
#include <WiFi.h>
#include <esp_heap_caps.h>

#include <algorithm>

#include "AppConfig.h"
#include "core/AssetPaths.h"
#include "core/ConfigRules.h"
#include "core/CoreEngine.h"
#include "core/ProvisioningPolicy.h"
#include "core/api/ApiRouter.h"
#include "core/api/SoundsApi.h"
#include "core/api/JsonWriter.h"
#include "core/api/StateJson.h"
#include "core/backup/RestoreApplier.h"
#include "core/payload/PayloadParser.h"
#include "core/render/Canvas.h"
#include "core/script/ScriptConfig.h"
#include "core/script/ScriptHost.h"
#include "core/script/ScriptServices.h"
#include "hal/IBoard.h"
#include "persistence/DeviceConfig.h"
#include "persistence/FsRestoreSink.h"
#include "persistence/SystemConfigApply.h"
#include "system/HeapCaps.h"
#include "system/HeapProbe.h"
#include "system/Log.h"
#include "transport/DeviceStateJson.h"
#include "transport/http/WebUiAsset.h"

namespace awtrix {

namespace {
// Permanent arena for ordinary API bodies. Script sources are far larger and get their own arena
// that is allocated per request and released again.
constexpr std::size_t kMaxBodyBytes = 8192;

constexpr int kArenaBodyThresholdBytes = 2048;

// Kept short while a raw body streams in: the whole device runs on one loop, so a stalled uploader
// must not hold it for seconds.
constexpr unsigned long kRawBodyIdleTimeoutMs = 500;

constexpr unsigned long kClientIdleTimeoutMs = 5000;

constexpr unsigned long kSilentClientGraceMs = 50;

std::size_t bodyCapFor(const std::string& method, const std::string& path) {
  return api::isRawBodyWrite(method, path) ? script::maxSourceBytes() : kMaxBodyBytes;
}

constexpr std::size_t kBodyCopyMarginBytes = 4 * 1024;

class RawWebServer : public WebServer {
 public:
  using WebServer::WebServer;
  void setRawReadTimeout(unsigned long ms) { _currentClient.Stream::setTimeout(ms); }

  // WebServer serves one client at a time. Browsers like to open a socket and send nothing, which
  // would stall every other request, so drop a silent client as soon as someone else is waiting.
  void handleClient() override {
    if (_currentStatus == HC_WAIT_READ && !_currentClient.available() &&
        millis() - _statusChange > kSilentClientGraceMs && _server.hasClient()) {
      _currentClient.stop();
    }
    WebServer::handleClient();
  }
};

const char* methodName(HTTPMethod m) {
  switch (m) {
    case HTTP_GET: return "GET";
    case HTTP_POST: return "POST";
    case HTTP_PUT: return "PUT";
    case HTTP_PATCH: return "PATCH";
    case HTTP_DELETE: return "DELETE";
    default: return "ANY";
  }
}

}

class HttpApiServer::BodyHandler : public RequestHandler {
 public:
  explicit BodyHandler(HttpApiServer& srv) : srv_(srv) {}
  bool canHandle(HTTPMethod m, String uri) override {
    return (m == HTTP_POST || m == HTTP_PUT || m == HTTP_PATCH) && uri.startsWith("/api/");
  }
  // Small bodies are cheap enough through WebServer's own "plain" argument; only larger ones are
  // worth streaming into the arena.
  bool canRaw(String) override {
    return srv_.server_->clientContentLength() > kArenaBodyThresholdBytes;
  }
  void raw(WebServer& server, String uri, HTTPRaw& raw) override {
    srv_.collectBody(server, uri, raw);
  }
  bool handle(WebServer&, HTTPMethod, String) override {
    srv_.dispatch();
    return true;
  }

 private:
  HttpApiServer& srv_;
};

namespace {

// ESP image header: byte 0 is the magic, bytes 12 and 13 hold the chip id, little endian.
constexpr size_t kEspImageHeaderBytes = 16;
constexpr size_t kEspImageChipIdOffset = 12;
constexpr uint8_t kEspImageMagic = 0xE9;
#if defined(AWTRIX_SOC_ESP32S3)
constexpr uint16_t kExpectedChipId = 0x0009;
#else
constexpr uint16_t kExpectedChipId = 0x0000;
#endif

const char* chipIdName(uint16_t id) {
  switch (id) {
    case 0x0000: return "ESP32";
    case 0x0002: return "ESP32-S2";
    case 0x0005: return "ESP32-C3";
    case 0x0009: return "ESP32-S3";
    case 0x000D: return "ESP32-C6";
    case 0x0010: return "ESP32-H2";
    default:     return "an unknown chip";
  }
}

const char* mimeFor(const std::string& path) {
  const size_t dot = path.rfind('.');
  const std::string ext = (dot == std::string::npos) ? "" : path.substr(dot);
  if (ext == ".jpg" || ext == ".jpeg") return "image/jpeg";
  if (ext == ".gif") return "image/gif";
  if (ext == ".png") return "image/png";
  if (ext == ".txt") return "text/plain";
  return "application/octet-stream";
}

class LittleFsRestoreSink : public backup::FsRestoreSink {
 public:
  using backup::FsRestoreSink::FsRestoreSink;
  bool beginFile(const std::string& path, std::string& err) override {
    const String p(path.c_str());
    const int slash = p.lastIndexOf('/');
    if (slash > 0) {
      const String dir = p.substring(0, slash);
      if (!LittleFS.exists(dir)) LittleFS.mkdir(dir);
    }
    file_ = LittleFS.open(p, "w");
    curPath_ = path;
    if (!file_) {
      err = "could not open for writing";
      return false;
    }
    return true;
  }
  bool writeFile(const uint8_t* data, std::size_t n) override {
    return file_ && file_.write(data, n) == n;
  }
  bool endFile() override {
    if (file_) file_.close();
    return true;
  }
  void abortFile() override {
    if (file_) file_.close();
    if (!curPath_.empty()) LittleFS.remove(String(curPath_.c_str()));
  }

 private:
  File file_;
  std::string curPath_;
};
}

void HttpApiServer::begin(uint16_t port, CoreEngine& engine, IBoard& board, Canvas& screen,
                          const std::string& uid, DeviceConfig& cfg, bool apMode) {
  engine_ = &engine;
  board_ = &board;
  screen_ = &screen;
  uid_ = uid;
  cfg_ = &cfg;
  apMode_ = apMode;
  server_ = new RawWebServer(port);
  static const char* kCollectHeaders[] = {"If-None-Match", "Content-Type"};
  server_->collectHeaders(kCollectHeaders, 2);
  // Each pair is (completion handler, per-chunk upload handler). The upload handler runs many times
  // while the body streams in and cannot answer the client; only the completion handler can.
  server_->on(
      "/update", HTTP_POST, [this]() { handleUpdateDone(); }, [this]() { handleUpdateUpload(); });
  server_->on(
      "/api/v1/files", HTTP_POST, [this]() { handleFileUploadDone(); },
      [this]() { handleFileUpload(); });
  server_->on(
      "/api/v1/restore", HTTP_POST, [this]() { handleRestoreDone(); },
      [this]() { handleRestoreUpload(); });
  server_->addHandler(new BodyHandler(*this));
  server_->onNotFound([this]() { dispatch(); });
  if (!bodyArena_.init(kMaxBodyBytes))
    logf("http: body arena allocation failed; body-carrying requests will be refused");
  server_->begin();
}

// Runs once per chunk of the multipart body. Failures are only recorded in the upload* flags here;
// handleFileUploadDone turns them into a status code afterwards.
void HttpApiServer::handleFileUpload() {
  HTTPUpload& up = server_->upload();
  if (up.status == UPLOAD_FILE_START) {
    if (uploadFile_) uploadFile_.close();
    uploadPathOk_ = false;
    uploadWriteOk_ = false;
    uploadPath_.clear();
    if (apMode_) { uploadAuthed_ = false; return; }
    uploadAuthed_ = !cfg_->authEnabled ||
                    server_->authenticate(cfg_->authUser.c_str(), cfg_->authPass.c_str());
    if (!uploadAuthed_) return;
    String fn = up.filename;
    if (!fn.startsWith("/")) {
      String dir = server_->hasArg("dir") ? server_->arg("dir") : String("/ICONS");
      if (!dir.startsWith("/")) dir = "/" + dir;
      fn = dir + "/" + fn;
    }
    uploadPathOk_ = assets::isWritable(std::string(fn.c_str()));
    if (!uploadPathOk_) return;
    const int slash = fn.lastIndexOf('/');
    if (slash > 0) {
      const String dir = fn.substring(0, slash);
      if (!LittleFS.exists(dir)) LittleFS.mkdir(dir);
    }
    uploadPath_ = fn.c_str();
    uploadContentOk_ = true;
    uploadContentChecked_ = false;
    uploadFile_ = LittleFS.open(fn, "w");
    uploadWriteOk_ = static_cast<bool>(uploadFile_);
  } else if (up.status == UPLOAD_FILE_WRITE) {
    // Sniff the first chunk only, which is enough to catch a file dropped into the wrong folder.
    if (!uploadContentChecked_ && up.currentSize > 0) {
      uploadContentChecked_ = true;
      uploadContentOk_ = assets::contentLooksValid(assets::kindFor(uploadPath_), up.buf,
                                                   static_cast<unsigned>(up.currentSize));
    }
    if (!uploadContentOk_) return;
    if (uploadWriteOk_ && uploadFile_ &&
        uploadFile_.write(up.buf, up.currentSize) != up.currentSize) {
      uploadWriteOk_ = false;
    }
  } else if (up.status == UPLOAD_FILE_END) {
    if (uploadFile_) uploadFile_.close();
    if ((!uploadContentOk_ || !uploadWriteOk_) && !uploadPath_.empty())
      LittleFS.remove(uploadPath_.c_str());
  } else if (up.status == UPLOAD_FILE_ABORTED) {
    if (uploadFile_) uploadFile_.close();
    if (!uploadPath_.empty()) LittleFS.remove(uploadPath_.c_str());
  }
}

void HttpApiServer::handleFileUploadDone() {
  addCorsHeaders(false);
  if (apMode_) {
    sendError(403, "forbidden", "file upload is disabled during provisioning");
    return;
  }
  if (!uploadAuthed_) { sendUnauthorized(); return; }
  if (!uploadPathOk_) {
    sendError(400, "invalidPath",
              "filename must be under /ICONS, /MELODIES or /PALETTES and contain no '..'");
    return;
  }
  if (!uploadContentOk_) {
    const std::string msg =
        std::string("file content does not match the target folder; expected ") +
        assets::acceptedFormats(assets::kindFor(uploadPath_));
    sendError(415, "unsupportedMediaType", msg.c_str());
    return;
  }
  if (!uploadWriteOk_) {
    sendError(500, "internalError", "file write failed (storage full?)");
    return;
  }
  if (onAssetsChanged_) onAssetsChanged_();
  sendJson(200, "{\"ok\":true}");
}

// The backup zip is piped chunk by chunk through the reader and applier straight onto the
// filesystem; there is nowhere near enough heap to hold the archive.
void HttpApiServer::handleRestoreUpload() {
  HTTPUpload& up = server_->upload();
  if (up.status == UPLOAD_FILE_START) {
    restoreStarted_ = false;
    restoreReader_.reset();
    restoreApplier_.reset();
    restoreSink_.reset();
    restoreAuthed_ = !cfg_->authEnabled ||
                     server_->authenticate(cfg_->authUser.c_str(), cfg_->authPass.c_str());
    if (!restoreAuthed_) return;
    restoreSink_.reset(new LittleFsRestoreSink(*cfg_, &engine_->state(), onConfigChanged_));
    restoreApplier_.reset(new backup::RestoreApplier(*restoreSink_));
    restoreReader_.reset(new backup::ZipReader(*restoreApplier_));
    restoreStarted_ = true;
  } else if (up.status == UPLOAD_FILE_WRITE) {
    if (restoreStarted_ && restoreReader_) restoreReader_->feed(up.buf, up.currentSize);
  } else if (up.status == UPLOAD_FILE_END) {
    if (restoreStarted_ && restoreReader_) restoreReader_->finish();
  }
}

void HttpApiServer::handleRestoreDone() {
  addCorsHeaders(false);
  if (!restoreAuthed_) {
    sendUnauthorized();
    return;
  }
  if (!restoreApplier_) {
    sendError(400, "badRequest", "no backup file received");
    return;
  }
  const backup::RestoreResult r = restoreApplier_->result();
  if ((r.icons || r.melodies || r.palettes) && onAssetsChanged_) onAssetsChanged_();
  if (r.ok) {
    logf("restore: applied wifi=%d system=%d settings=%d apploop=%d icons=%d melodies=%d "
         "palettes=%d scripts=%d (%u warning(s))",
         r.wifi, r.system, r.settings, r.appLoop, r.icons, r.melodies, r.palettes, r.scripts,
         static_cast<unsigned>(r.warnings.size()));
  } else {
    logf("restore: rejected - %s", r.error.c_str());
  }
  sendJson(r.ok ? 200 : 400, r.toJson());
  restoreReader_.reset();
  restoreApplier_.reset();
  restoreSink_.reset();
}

void HttpApiServer::dropRawBody() {
  bodyArena_.reset();
  sourceArena_.release();
}

// Called by WebServer for every raw-body chunk. Script sources use a second arena that is
// allocated at RAW_START and released again on completion, since it dwarfs the fixed body arena.
void HttpApiServer::collectBody(WebServer& server, const String& uri, HTTPRaw& raw) {
  const std::string method = methodName(server.method());
  const std::string path = uri.c_str();
  const bool rawSource = api::isRawBodyWrite(method, path);
  BodyArena& arena = rawSource ? sourceArena_ : bodyArena_;
  switch (raw.status) {
    case RAW_START:
      static_cast<RawWebServer&>(server).setRawReadTimeout(kRawBodyIdleTimeoutMs);
      if (rawSource) arena.init(script::maxSourceBytes());
      arena.open(rawSource ? script::maxSourceBytes() : bodyCapFor(method, path));
      return;
    case RAW_WRITE:
      arena.append(raw.buf, raw.currentSize);
      return;
    case RAW_END:
      static_cast<RawWebServer&>(server).setRawReadTimeout(kClientIdleTimeoutMs);
      arena.finish();
      return;
    case RAW_ABORTED:
    default:
      static_cast<RawWebServer&>(server).setRawReadTimeout(kClientIdleTimeoutMs);
      if (rawSource) arena.release();
      else arena.reset();
      return;
  }
}

void HttpApiServer::handleUpdateUpload() {
  HTTPUpload& up = server_->upload();
  if (apMode_) return;
  if (up.status == UPLOAD_FILE_START) {
    uploadWriteOk_ = false;
    uploadAuthed_ = !cfg_->authEnabled ||
                    server_->authenticate(cfg_->authUser.c_str(), cfg_->authPass.c_str());
    if (!uploadAuthed_) return;
    const size_t contentLen = server_->clientContentLength();
    if (contentLen > 0 && contentLen > ESP.getFreeSketchSpace()) return;
    updateChipError_.clear();
    uploadContentChecked_ = false;
    uploadWriteOk_ = Update.begin(UPDATE_SIZE_UNKNOWN);
  } else if (up.status == UPLOAD_FILE_WRITE) {
    // Catch an image built for a different SoC from the very first chunk, before any of it reaches
    // the OTA partition.
    if (uploadWriteOk_ && !uploadContentChecked_ && up.currentSize >= kEspImageHeaderBytes) {
      uploadContentChecked_ = true;
      if (up.buf[0] != kEspImageMagic) {
        updateChipError_ = "not a firmware image (missing the 0xE9 header magic)";
      } else {
        const uint16_t chip = static_cast<uint16_t>(up.buf[kEspImageChipIdOffset]) |
                              static_cast<uint16_t>(up.buf[kEspImageChipIdOffset + 1] << 8);
        if (chip != kExpectedChipId)
          updateChipError_ = std::string("image is built for ") + chipIdName(chip) +
                             ", this device is an " + chipIdName(kExpectedChipId);
      }
      if (!updateChipError_.empty()) {
        logf("update: refused - %s", updateChipError_.c_str());
        uploadWriteOk_ = false;
        Update.abort();
        return;
      }
    }
    if (uploadWriteOk_ && Update.write(up.buf, up.currentSize) != up.currentSize) {
      uploadWriteOk_ = false;
    }
  } else if (up.status == UPLOAD_FILE_END) {
    if (uploadWriteOk_) {
      if (!Update.end(true)) uploadWriteOk_ = false;
    } else {
      Update.abort();
    }
  }
}

void HttpApiServer::handleUpdateDone() {
  addCorsHeaders(false);
  if (apMode_) {
    sendError(403, "forbidden", "firmware update is disabled during provisioning");
    return;
  }
  if (!uploadAuthed_) {
    sendUnauthorized();
    return;
  }
  if (!updateChipError_.empty()) {
    sendError(400, "wrongChip", updateChipError_.c_str());
    return;
  }
  if (!uploadWriteOk_ || Update.hasError()) {
    sendError(500, "internalError", "firmware update failed (bad image or storage full)");
    return;
  }
  sendJson(200, "{\"ok\":true}");
  engine_->execute(Command(CommandType::Reboot));
}

void HttpApiServer::tick() {
  if (server_) server_->handleClient();
}

// Sends the 401 itself when authentication fails, so callers only have to bail out.
bool HttpApiServer::authOk() {
  if (!cfg_->authEnabled) return true;
  if (server_->authenticate(cfg_->authUser.c_str(), cfg_->authPass.c_str())) return true;
  sendUnauthorized();
  return false;
}

void HttpApiServer::sendUnauthorized() {
  server_->sendHeader("WWW-Authenticate", "Basic realm=\"AWTRIX NG\"");
  sendJson(401, api::errorJson("unauthorized", "authentication required"));
}

// The probe:: calls bracket heap-accounting windows and compile away unless AWTRIX_HEAP_PROBE is
// defined; they are sprinkled through the request path to attribute allocations to a phase.
void HttpApiServer::sendJson(int status, const std::string& body) {
  probe::report("resp:build", 128);
  probe::begin();
  server_->send(status, "application/json", body.c_str());
  probe::report("resp:send", 128);
  probe::begin();
}

void HttpApiServer::sendResult(const api::HttpResult& res) {
  if (res.retryAfterSeconds > 0)
    server_->sendHeader("Retry-After", String(res.retryAfterSeconds));
  server_->send(res.status, res.contentType, res.body.c_str());
}

void HttpApiServer::sendError(int status, const char* code, const char* message) {
  sendJson(status, api::errorJson(code, message));
}

void HttpApiServer::addCorsHeaders(bool preflight) {
  server_->sendHeader("Access-Control-Allow-Origin", "*");
  if (preflight) {
    server_->sendHeader("Access-Control-Allow-Methods", "GET, POST, PUT, PATCH, DELETE, OPTIONS");
    server_->sendHeader("Access-Control-Allow-Headers", "Content-Type, Authorization");
    server_->sendHeader("Access-Control-Allow-Private-Network", "true");
    server_->sendHeader("Access-Control-Max-Age", "600");
  }
}

void HttpApiServer::dispatch() {
  if (server_->method() == HTTP_OPTIONS) {
    addCorsHeaders(true);
    dropRawBody();
    server_->send(204, "text/plain", "");
    return;
  }
  addCorsHeaders(false);

  if (rejectedByPortal()) return;

  if (!authOk()) {
    dropRawBody();
    return;
  }

  Request req;
  req.method = methodName(server_->method());
  req.path = server_->uri().c_str();
  req.get = (server_->method() == HTTP_GET);

  if (takeBody(req)) return;
  if (rejectedByPolicy(req)) return;

  if (serveWebUi(req)) return;
  if (serveAsset(req)) return;

  probe::report("req:body", 128);
  probe::begin();

  if (serveCommand(req)) return;
  if (serveState(req)) return;
  if (serveDiagnostics(req)) return;
  if (serveSystem(req)) return;
  if (serveSounds(req)) return;
  if (serveFiles(req)) return;

  sendError(404, "notFound", "unknown route");
}

// Captive-portal probes arrive with a foreign Host header. Bounce them to the AP address so the
// phone pops up the provisioning page instead of declaring the network broken.
bool HttpApiServer::rejectedByPortal() {
  if (!apMode_) return false;
  const String apIp = WiFi.softAPIP().toString();
  const String host = server_->hostHeader();
  if (!host.length() || host == apIp || host.startsWith(apIp + ":")) return false;
  dropRawBody();
  server_->sendHeader("Location", String("http://") + apIp + "/", true);
  server_->send(302, "text/plain", "");
  return true;
}

// Moves whatever the arena collected into req.body. Returns true when it already answered the
// client, in which case the caller must stop.
bool HttpApiServer::takeBody(Request& req) {
  const bool rawSource = api::isRawBodyWrite(req.method, req.path);
  BodyArena& arena = rawSource ? sourceArena_ : bodyArena_;
  const std::size_t bodyCap = bodyCapFor(req.method, req.path);

  if (rawSource && !arena.ready() && arena.state() == BodyArena::State::Overflow) {
    arena.release();
    sendJson(507, api::errorJson("insufficientStorage",
                                 "not enough memory to receive the script source; "
                                 "delete a script or reboot",
                                 "source"));
    return true;
  }
  if (arena.state() == BodyArena::State::Overflow) {
    arena.reset();
    if (rawSource) {
      arena.release();
      const std::string msg = "script source exceeds " + std::to_string(bodyCap) + " bytes";
      sendJson(413, api::errorJson("payloadTooLarge", msg, "source"));
    } else {
      const std::string msg = "body exceeds " + std::to_string(bodyCap) + " bytes";
      sendError(413, "payloadTooLarge", msg.c_str());
    }
    return true;
  }
  if (arena.state() != BodyArena::State::Idle) {
    const std::string_view received = arena.view();
    if (!received.empty()) {
      // Refuse instead of fragmenting: the copy needs one contiguous block, plus margin for
      // whatever parsing and dispatch will allocate on top of it.
      if (received.size() > 15 &&
          heap_caps_get_largest_free_block(kGuardHeapCaps) <
              received.size() + kBodyCopyMarginBytes) {
        arena.reset();
        if (rawSource) arena.release();
        sendError(507, "insufficientStorage",
                  "not enough free memory to act on a body this size; delete a script or reboot");
        return true;
      }
      req.body.assign(received.data(), received.size());
    }
    arena.reset();
    if (rawSource) arena.release();
    return false;
  }
  if (server_->hasArg("plain")) {
    req.body = server_->arg("plain").c_str();
    if (req.body.size() > bodyCap) {
      const std::string msg = "body exceeds " + std::to_string(bodyCap) + " bytes";
      sendError(413, "payloadTooLarge", msg.c_str());
      return true;
    }
  }
  return false;
}

bool HttpApiServer::rejectedByPolicy(const Request& req) {
  if (apMode_ && !provisioning::apModeAllows(req.method, req.path)) {
    sendError(403, "forbidden", "not available during provisioning");
    return true;
  }
  if ((server_->method() == HTTP_PUT || server_->method() == HTTP_PATCH) &&
      server_->hasHeader("Content-Type") && !api::isRawBodyWrite(req.method, req.path)) {
    const String ct = server_->header("Content-Type");
    if (ct.length() && !ct.startsWith("application/json")) {
      sendError(415, "unsupportedMediaType", "Content-Type must be application/json");
      return true;
    }
  }
  return false;
}

// The web UI is a gzip blob in flash, sent verbatim without decompressing. The ETag never changes
// within a build, so a repeat visit costs a 304 instead of the whole transfer.
bool HttpApiServer::serveWebUi(const Request& req) {
  if (req.path != "/" && req.path != "/index.html") return false;
  if (server_->header("If-None-Match") == WEBUI_ETAG) {
    server_->send(304, "text/plain", "");
    return true;
  }
  server_->sendHeader("Content-Encoding", "gzip");
  server_->sendHeader("ETag", WEBUI_ETAG);
  server_->sendHeader("Cache-Control", "no-cache");
  server_->send_P(200, "text/html", reinterpret_cast<const char*>(WEBUI_GZ), WEBUI_GZ_LEN);
  return true;
}

bool HttpApiServer::serveAsset(const Request& req) {
  if (!req.get) return false;
  if (!assets::isServable(req.path) && (apMode_ || !assets::isBackupReadable(req.path)))
    return false;
  File f = LittleFS.open(req.path.c_str(), "r");
  if (!f || f.isDirectory()) {
    if (f) f.close();
    sendError(404, "notFound", "file not found");
    return true;
  }
  server_->sendHeader("Cache-Control", "max-age=3600");
  server_->streamFile(f, mimeFor(req.path));
  f.close();
  return true;
}

bool HttpApiServer::serveCommand(Request& req) {
  Command cmd;
  api::HttpResult immediate;
  switch (api::routeHttp(req.method, req.path, std::move(req.body), cmd, immediate)) {
    case api::RouteOutcome::Respond:
      probe::report("req:route", 128);
      probe::begin();
      sendJson(immediate.status, immediate.body);
      probe::report("req:resp", 128);
      probe::begin();
      return true;
    case api::RouteOutcome::Routed: {
      probe::report("req:route", 128);
      probe::begin();
      const DispatchResult r = engine_->execute(cmd);
      probe::report("req:exec", 128);
      probe::begin();
      logdbg("http %s %s -> %d", req.method.c_str(), req.path.c_str(), static_cast<int>(r));
      if (cmd.type == CommandType::SetSettings && r == DispatchResult::Ok) {
        sendJson(200, buildSettingsJson(*engine_));
      } else {
        sendResult(api::httpResponse(cmd, r, engine_->lastDetail()));
      }
      probe::report("req:resp", 128);
      probe::begin();
      return true;
    }
    case api::RouteOutcome::NoMatch:
    default:
      return false;
  }
}

bool HttpApiServer::serveState(const Request& req) {
  if (!req.get) return false;
  const std::string& path = req.path;

  if (path == "/api/v1/settings") {
    sendJson(200, buildSettingsJson(*engine_));
    return true;
  }
  if (path == "/api/v1/device") {
    sendJson(200, buildDeviceStateJson(*engine_, *board_, uid_, scripts_ != nullptr));
    return true;
  }
  if (path == "/api/v1/display") {
    sendJson(200, buildDisplayJson(*engine_));
    return true;
  }
  if (path == "/api/v1/display/screen") {
    sendJson(200, buildScreenJson(*screen_));
    return true;
  }
  if (path == "/api/v1/capabilities") {
    sendJson(200, *capabilitiesJson_);
    return true;
  }
  if (path == "/api/v1/version") {
    sendJson(200, std::string("{\"version\":\"") + AWTRIX_NG_VERSION + "\"}");
    return true;
  }
  if (path == "/version") {
    server_->send(200, "text/plain", AWTRIX_NG_VERSION);
    return true;
  }

  if (path == "/api/v1/radio") {
    respBuf_.clear();
    appendRadioJson(respBuf_, *engine_);
    sendJson(200, respBuf_);
    return true;
  }
  if (path == "/api/v1/apps") {
    respBuf_.clear();
    // With the script host down we can still list what is sitting on flash, so the UI does not go
    // blank after a scripting failure.
    if (scripts_ || !storedScripts_) {
      appendAppsJson(respBuf_, *engine_, scripts_);
    } else {
      const std::vector<script::StoredScript> stored = storedScripts_();
      appendAppsJson(respBuf_, *engine_, nullptr, &stored);
    }
    sendJson(200, respBuf_);
    return true;
  }
  if (path == "/api/v1/scripts/shared") {
    if (!scripts_) {
      sendError(503, "unavailable", "scripting is not available");
      return true;
    }
    respBuf_.clear();
    appendSharedStateJson(respBuf_, scripts_->sharedSnapshot());
    sendJson(200, respBuf_);
    return true;
  }

  if (path.rfind("/api/v1/apps/script/", 0) == 0) {
    const std::string name = path.substr(std::string("/api/v1/apps/script/").size());
    if (!scriptSource_) {
      sendError(503, "unavailable", "scripting is not available");
      return true;
    }
    if (!api::isValidAppName(name)) {
      sendJson(400, api::errorJson("invalidName", "name must match [A-Za-z0-9_-]{1,32}", "name"));
      return true;
    }
    std::string source;
    if (!scriptSource_(name, source)) {
      sendError(404, "notFound", "no such script");
      return true;
    }
    server_->send(200, "text/plain", source.c_str());
    return true;
  }

  {
    const std::string name = api::configAppName(path);
    if (!name.empty()) {
      respBuf_.clear();
      sendJson(script::configResponse(name, scriptSource_, scriptStore_, respBuf_), respBuf_);
      return true;
    }
  }
  return false;
}

bool HttpApiServer::serveDiagnostics(const Request& req) {
  if (!req.get) return false;

  // Scanning takes seconds and would block the loop, so the first call starts it and answers 202;
  // the caller polls until a result array comes back.
  if (req.path == "/api/v1/system/wifi-scan") {
    const int n = WiFi.scanComplete();
    if (n == WIFI_SCAN_FAILED) {
      WiFi.scanNetworks(true);
      sendJson(202, "{\"scanning\":true}");
      return true;
    }
    if (n == WIFI_SCAN_RUNNING) {
      sendJson(202, "{\"scanning\":true}");
      return true;
    }
    std::string out;
    api::JsonWriter w(out);
    w.beginArray();
    for (int i = 0; i < n; ++i) {
      w.beginObject();
      w.member("ssid", std::string(WiFi.SSID(i).c_str()));
      w.member("rssi", static_cast<int>(WiFi.RSSI(i)));
      w.member("enc", WiFi.encryptionType(i) != WIFI_AUTH_OPEN);
      w.endObject();
    }
    w.endArray();
    WiFi.scanDelete();
    sendJson(200, out);
    return true;
  }

  if (req.path == "/api/v1/logs") {
    const uint32_t after =
        server_->hasArg("after") ? strtoul(server_->arg("after").c_str(), nullptr, 10) : 0;
    sendJson(200, logbuf::jsonAfter(after));
    return true;
  }
  return false;
}

bool HttpApiServer::serveSystem(const Request& req) {
  if (req.path != "/api/v1/system") return false;
  if (req.get) {
    const bool withSecrets = server_->hasArg("secrets") && !apMode_;
    sendJson(200, systemJson(withSecrets));
    return true;
  }
  if (req.method == "PUT") {
    if (!api::isWellFormed(req.body)) {
      sendError(400, "invalidJson", "request body is not valid JSON");
      return true;
    }
    DeviceConfig merged = *cfg_;
    sysconfig::ApplyError ae;
    int applied = 0;
    if (!sysconfig::apply(merged, api::JsonReader(req.body), applied, ae)) {
      sendJson(ae.status, api::errorJson(ae.code.c_str(), ae.message, ae.field));
      return true;
    }
    *cfg_ = merged;
    cfg_->save();
    logf("config: %d field(s) saved via web UI", applied);
    if (onConfigChanged_) onConfigChanged_();
    sendJson(200, systemJson());
    return true;
  }
  sendError(405, "methodNotAllowed", "allowed method(s): GET, PUT");
  return true;
}

bool HttpApiServer::serveSounds(const Request& req) {
  const std::string& path = req.path;

  if (path == "/api/v1/sounds") {
    if (!req.get) {
      sendError(405, "methodNotAllowed", "allowed method(s): GET");
      return true;
    }
    // Chunked with an unknown length: every melody carries its full text, so building the reply as
    // one string would not fit the heap.
    server_->setContentLength(CONTENT_LENGTH_UNKNOWN);
    server_->send(200, "application/json", "");
    server_->sendContent("{\"melodies\":[");
    File root = LittleFS.open("/MELODIES");
    bool first = true;
    if (root && root.isDirectory())
      for (File f = root.openNextFile(); f; f = root.openNextFile()) {
        const std::string name = api::sounds::nameFromFile(std::string(f.name()));
        if (name.empty()) continue;
        std::string content;
        content.reserve(f.size());
        while (f.available()) content.push_back(static_cast<char>(f.read()));
        const std::string entry =
            (first ? "" : ",") +
            api::sounds::entryJson(name, content, static_cast<uint32_t>(f.size()));
        server_->sendContent(entry.c_str());
        first = false;
      }
    const String tail = String("],\"usedBytes\":") + LittleFS.usedBytes() +
                        ",\"totalBytes\":" + LittleFS.totalBytes() + "}";
    server_->sendContent(tail);
    server_->sendContent("");
    return true;
  }

  if (path.rfind("/api/v1/sounds/", 0) == 0) {
    const std::string name = path.substr(sizeof("/api/v1/sounds/") - 1);
    const String file = String(api::sounds::pathFor(name).c_str());

    if (req.method == "PUT") {
      const api::sounds::PutResult r = api::sounds::prepareWrite(name, req.body);
      if (!r.ok) {
        sendJson(r.status, api::errorJson(r.code.c_str(), r.message, r.field));
        return true;
      }
      const bool existed = LittleFS.exists(file);
      File f = LittleFS.open(file, "w");
      if (!f || f.print(r.content.c_str()) != r.content.size()) {
        if (f) f.close();
        sendError(507, "insufficientStorage", "could not write the melody; the flash is full");
        return true;
      }
      f.close();
      if (onAssetsChanged_) onAssetsChanged_();
      sendJson(existed ? 200 : 201, "{\"ok\":true}");
      return true;
    }

    if (server_->method() == HTTP_DELETE) {
      if (!api::sounds::nameFromFile(name + ".txt").empty() && LittleFS.remove(file)) {
        if (onAssetsChanged_) onAssetsChanged_();
        sendJson(200, "{\"ok\":true}");
      } else {
        sendError(404, "notFound", "melody not found");
      }
      return true;
    }

    sendError(405, "methodNotAllowed", "allowed method(s): PUT, DELETE");
    return true;
  }
  return false;
}

bool HttpApiServer::serveFiles(const Request& req) {
  if (req.path != "/api/v1/files") return false;

  if (req.get) {
    const String dir = server_->hasArg("dir") ? server_->arg("dir") : String("/ICONS");
    server_->setContentLength(CONTENT_LENGTH_UNKNOWN);
    server_->send(200, "application/json", "");
    server_->sendContent("{\"files\":[");
    File root = LittleFS.open(dir);
    bool first = true;
    if (root && root.isDirectory())
      for (File f = root.openNextFile(); f; f = root.openNextFile()) {
        std::string entry = first ? "" : ",";
        first = false;
        api::JsonWriter ew(entry);
        ew.beginObject();
        ew.member("name", std::string(f.name()));
        ew.member("size", static_cast<unsigned long>(f.size()));
        ew.endObject();
        server_->sendContent(entry.c_str());
      }
    const String tail = String("],\"usedBytes\":") + LittleFS.usedBytes() +
                        ",\"totalBytes\":" + LittleFS.totalBytes() + "}";
    server_->sendContent(tail);
    server_->sendContent("");
    return true;
  }

  if (server_->method() == HTTP_DELETE) {
    const String fn = server_->hasArg("path") ? server_->arg("path") : String("");
    if (!assets::isWritable(std::string(fn.c_str()))) {
      sendError(400, "invalidPath",
                "path must be under /ICONS, /MELODIES or /PALETTES and contain no '..'");
      return true;
    }
    if (LittleFS.remove(fn)) {
      if (onAssetsChanged_) onAssetsChanged_();
      sendJson(200, "{\"ok\":true}");
    } else {
      sendError(404, "notFound", "file not found");
    }
    return true;
  }

  sendError(405, "methodNotAllowed", "allowed method(s): GET, POST, DELETE");
  return true;
}

std::string HttpApiServer::systemJson(bool withSecrets) const {
  std::string out;
  out.reserve(1536);
  api::JsonWriter w(out);
  w.beginObject();
  cfg_->write(w, withSecrets);
  w.endObject();
  return out;
}

}
