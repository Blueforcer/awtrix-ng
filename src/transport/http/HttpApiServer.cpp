#include "transport/http/HttpApiServer.h"

#include <LittleFS.h>
#include <Update.h>
#include <WiFi.h>
#include <dirent.h>
#include <esp_heap_caps.h>
#include <mbedtls/sha256.h>
#include <unistd.h>
// Included by name rather than left to the Arduino headers: the image marker below reads
// CONFIG_SPIRAM_MODE_QUAD out of it, and an absent macro reads as octal - which is the wrong
// answer to be arriving at by accident.
#include <sdkconfig.h>

#include <algorithm>
#include <cstring>

#include "AppConfig.h"
#include "core/AssetPaths.h"
#include "core/ConfigRules.h"
#include "core/CoreEngine.h"
#include "core/ProvisioningPolicy.h"
#include "core/api/ApiRouter.h"
#include "core/api/AssetApi.h"
#include "core/api/HttpProtocol.h"
#include "core/api/FilesApi.h"
#include "core/api/DiagnosticsApi.h"
#include "core/api/MelodiesApi.h"
#include "core/api/JsonStream.h"
#include "core/api/JsonWriter.h"
#include "core/api/StateJson.h"
#include "core/api/StateReadApi.h"
#include "core/api/RestoreApi.h"
#include "core/api/ScriptSoundsApi.h"
#include "core/backup/RestoreApplier.h"
#include "core/payload/PayloadParser.h"
#include "core/render/Canvas.h"
#include "core/script/ScriptConfig.h"
#include "core/script/ScriptHeap.h"
#include "core/script/ScriptHost.h"
#include "core/script/ScriptServices.h"
#include "hal/IBoard.h"
#include "media/AssetFile.h"
#include "persistence/DeviceConfig.h"
#include "persistence/CommandPersistence.h"
#include "persistence/DocumentFile.h"
#include "persistence/Filesystem.h"
#include "persistence/FsRestoreSink.h"
#include "persistence/IconOriginsStore.h"
#include "persistence/SystemConfigApply.h"
#include "persistence/SystemConfigApi.h"
#include "persistence/VfsFile.h"
#include "system/HeapCaps.h"
#include "system/HeapProbe.h"
#include "transport/http/UpdateImage.h"
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

constexpr std::size_t kBodyCopyMarginBytes = 4 * 1024;

constexpr std::size_t kListBatchBytes = 1024;
constexpr std::size_t kListEntryReserveBytes = 128;

#include "transport/http/HttpAdapters.inc"

}

class HttpApiServer::BodyHandler : public RequestHandler {
 public:
  explicit BodyHandler(HttpApiServer& srv) : srv_(srv) {}
  bool canHandle(HTTPMethod m, String uri) override {
    return (m == HTTP_POST || m == HTTP_PUT || m == HTTP_PATCH) && uri.startsWith("/api/");
  }
  // Raw into the arena: bodies above kArenaBodyThresholdBytes and every form-encoded body.
  bool canRaw(String) override {
    return srv_.server_->clientContentLength() > kArenaBodyThresholdBytes ||
           srv_.server_->header("Content-Type").startsWith("application/x-www-form-urlencoded");
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

class HttpApiServer::UploadHandler : public RequestHandler {
 public:
  explicit UploadHandler(HttpApiServer& srv) : srv_(srv) {}
  bool canHandle(HTTPMethod method, String uri) override {
    if (method != HTTP_POST || !canUpload(uri)) return false;
    // Parsing.cpp selects a handler before reading headers. Only reset state here;
    // authentication and method override checks must wait until the upload callback.
    srv_.resetMultipartRequest();
    return true;
  }
  bool canUpload(String uri) override {
    // WebServer calls this repeatedly while parsing a multipart body. Keep it pure.
    return api::isUploadRoute(uri.c_str());
  }
  bool canRaw(String) override { return true; }
  void raw(WebServer& server, String, HTTPRaw& raw) override {
    // These routes require multipart files; other bodies are discarded in WebServer's raw buffer.
    if (raw.status == RAW_START)
      static_cast<RawWebServer&>(server).setRawReadTimeout(kRawBodyIdleTimeoutMs);
    else if (raw.status == RAW_END || raw.status == RAW_ABORTED)
      static_cast<RawWebServer&>(server).setRawReadTimeout(kClientIdleTimeoutMs);
  }
  void upload(WebServer&, String uri, HTTPUpload&) override {
    if (uri == "/update") srv_.handleUpdateUpload();
    else if (uri == "/api/v1/restore") srv_.handleRestoreUpload();
    else srv_.handleFileUpload();
  }
  bool handle(WebServer&, HTTPMethod, String uri) override {
    if (uri == "/update") srv_.handleUpdateDone();
    else if (uri == "/api/v1/restore") srv_.handleRestoreDone();
    else srv_.handleFileUploadDone();
    srv_.multipartRequestActive_ = false;
    return true;
  }

 private:
  HttpApiServer& srv_;
};

namespace {

#include "transport/http/HttpUploadStorage.inc"

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
  const char* kCollectHeaders[] = {"If-None-Match", "Content-Type",
      api::kMethodOverrideHeader, "Host", "Origin", "Sec-Fetch-Site"};
  server_->collectHeaders(kCollectHeaders, 6);
  // Select/reset each multipart request before parsing its body, including empty requests.
  server_->addHandler(new UploadHandler(*this));
  server_->addHandler(new BodyHandler(*this));
  server_->onNotFound([this]() { dispatch(); });
  if (!bodyArena_.init(kMaxBodyBytes))
    logf("http: body arena allocation failed; body-carrying requests will be refused");
  server_->begin();
}

#include "transport/http/HttpAssetUploads.inc"
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
      if (apMode_) {
        const auto resolved = api::resolveHttpMethod(method, path,
            server.header(api::kMethodOverrideHeader).c_str());
        if (provisioningVerdict(resolved.method, path, {}, !resolved.error) !=
            provisioning::Verdict::Allow) {
          if (rawSource) arena.release();
          else arena.reset();
          return;
        }
      }
      // A small script must not pay for the whole heap ceiling up front: this arena is alive
      // at the same time as the source copy and the install reserve. Read once, into a member,
      // so takeBody() can report it later without re-measuring a heap the upload has since spent.
      if (rawSource) {
        sourceCeiling_ = script::heap::growthBudget();
        openSourceArena(arena, server.clientContentLength(), sourceCeiling_);
      } else {
        arena.open(kMaxBodyBytes);
      }
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

// Walks the upload for the marker every AWTRIX NG image carries, a byte at a time so one split
// across two chunks is still found. It stops at the first: a firmware image holds exactly one, and
// an image from before the marker existed holds none - which is read as "says nothing" and let
// through, so a downgrade to an older release still works.
#include "transport/http/HttpFirmwareUpload.inc"
void HttpApiServer::tick() {
  if (!server_) return;
  server_->handleClient();
  // WebServer parses and handles a request synchronously. A selected upload handler
  // still marked active here means parsing returned early without its completion call.
  if (multipartRequestActive_) {
    fileUpload_.abort();
    updateRequest_.abort();
    restoreRequest_.abort();
    cleanupRestore();
    multipartRequestActive_ = false;
  }
}

// Sends the 401 itself when authentication fails, so callers only have to bail out.
#include "transport/http/HttpRoutes.inc"

}
