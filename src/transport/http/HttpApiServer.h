#pragma once

#include <FS.h>
#include <WebServer.h>

#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <vector>

#include "core/api/ApiRouter.h"
#include "core/script/ScriptMeta.h"
#include "core/backup/RestoreApplier.h"
#include "transport/http/BodyArena.h"

namespace awtrix::script {
class ScriptHost;
}

namespace awtrix {

class CoreEngine;
class IBoard;
class Canvas;
struct DeviceConfig;

class HttpApiServer {
 public:
  void begin(uint16_t port, CoreEngine& engine, IBoard& board, Canvas& screen, const std::string& uid,
             DeviceConfig& cfg, bool apMode);
  void tick();
  void setOnConfigChanged(std::function<void()> cb) { onConfigChanged_ = std::move(cb); }
  void setCapabilitiesJson(std::shared_ptr<const std::string> j) {
    capabilitiesJson_ = std::move(j);
  }
  void setOnAssetsChanged(std::function<void()> cb) { onAssetsChanged_ = std::move(cb); }
  using ScriptSourceFn = std::function<bool(const std::string& name, std::string& out)>;
  using StoredScriptsFn = std::function<std::vector<script::StoredScript>()>;
  void setScripts(const script::ScriptHost* host, ScriptSourceFn readSource,
                  ScriptSourceFn readStore, StoredScriptsFn stored = nullptr) {
    scripts_ = host;
    scriptSource_ = std::move(readSource);
    scriptStore_ = std::move(readStore);
    storedScripts_ = std::move(stored);
  }

 private:
  class BodyHandler;

  struct Request {
    std::string method;
    std::string path;
    std::string body;
    bool get = false;
  };

  void dispatch();

  bool rejectedByPortal();
  bool rejectedByPolicy(const Request& req);
  bool takeBody(Request& req);

  bool serveWebUi(const Request& req);
  bool serveAsset(const Request& req);
  bool serveCommand(Request& req);
  bool serveState(const Request& req);
  bool serveDiagnostics(const Request& req);
  bool serveSystem(const Request& req);
  bool serveSounds(const Request& req);
  bool serveFiles(const Request& req);

  bool authOk();
  void collectBody(WebServer& server, const String& uri, HTTPRaw& raw);
  void dropRawBody();
  void handleUpdateUpload();
  void scanImageMarker(const uint8_t* buf, size_t len);
  void handleUpdateDone();
  void handleFileUpload();
  void handleFileUploadDone();
  void handleRestoreUpload();
  void handleRestoreDone();
  void sendJson(int status, const std::string& body);
  void sendResult(const api::HttpResult& res);
  void sendError(int status, const char* code, const char* message);
  void sendUnauthorized();
  void addCorsHeaders(bool preflight);
  // Carried across the per-chunk upload callbacks, which cannot answer the client; the matching
  // handle*Done() reads them once the body is in and picks the status code.
  bool uploadAuthed_ = false;
  bool uploadPathOk_ = false;
  bool uploadWriteOk_ = false;
  bool uploadContentOk_ = true;
  bool uploadContentChecked_ = false;
  std::string updateImageError_;
  // The image marker straddles chunk boundaries as readily as it sits inside one, so the match runs
  // a byte at a time and its progress lives here between chunks.
  uint8_t markerMatched_ = 0;
  bool markerCapturing_ = false;
  bool markerRead_ = false;
  std::string markerVariant_;
  std::string uploadPath_;
  File uploadFile_;
  BodyArena bodyArena_;
  BodyArena sourceArena_;
  bool restoreAuthed_ = false;
  bool restoreStarted_ = false;
  std::unique_ptr<backup::RestoreSink> restoreSink_;
  std::unique_ptr<backup::RestoreApplier> restoreApplier_;
  std::unique_ptr<backup::ZipReader> restoreReader_;
  std::string systemJson(bool withSecrets = false) const;

  WebServer* server_ = nullptr;
  CoreEngine* engine_ = nullptr;
  IBoard* board_ = nullptr;
  Canvas* screen_ = nullptr;
  DeviceConfig* cfg_ = nullptr;
  std::function<void()> onConfigChanged_;
  std::string uid_;
  std::shared_ptr<const std::string> capabilitiesJson_ = std::make_shared<const std::string>("{}");
  std::function<void()> onAssetsChanged_;
  const script::ScriptHost* scripts_ = nullptr;
  ScriptSourceFn scriptSource_;
  ScriptSourceFn scriptStore_;
  StoredScriptsFn storedScripts_;
  std::string respBuf_;
  bool apMode_ = false;
};

}
