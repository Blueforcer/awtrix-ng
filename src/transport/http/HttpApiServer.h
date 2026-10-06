#pragma once

#include <FS.h>
#include <WebServer.h>

#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <vector>

#include "core/DeviceCapabilities.h"
#include "core/ProvisioningPolicy.h"
#include "core/api/HttpExchange.h"
#include "core/api/AssetApi.h"
#include "core/api/AssetUploadSession.h"
#include "core/api/ScriptSoundsApi.h"
#include "core/api/SingleUploadRequest.h"
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
  void setDeviceCapabilities(const DeviceCapabilities& caps) { deviceCapabilities_ = caps; }
  void setOnAssetsChanged(std::function<void()> cb) { onAssetsChanged_ = std::move(cb); }
  // Called with api::errorEvent() output for every rejected command.
  void setOnError(std::function<void(const std::string& event)> cb) { onError_ = std::move(cb); }
  // Asked with a path before a file there is deleted or replaced.
  void setReleaseFiles(std::function<void(const std::string& path)> release) {
    releaseFiles_ = std::move(release);
  }
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
  class UploadHandler;

  struct Request {
    std::string method;
    std::string path;
    std::string body;
    bool get = false;
  };

  void dispatch();
  api::Request apiRequest(const Request& req) const;

  provisioning::Verdict provisioningVerdict(std::string_view method, std::string_view path,
      std::string_view body = {}, bool methodValid = true) const;
  bool rejectedByProvisioning(const Request& req, bool methodValid = true);
  bool rejectedByPolicy(const Request& req);
  bool takeBody(Request& req);

  bool serveWebUi(const Request& req);
  bool serveCommand(Request& req);
  bool serveState(const Request& req);
  bool serveDiagnostics(const Request& req);
  bool serveSystem(const Request& req);
  bool serveFiles(const Request& req);
  api::scriptsounds::InstalledScripts installedScripts() const;

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
  void resetMultipartRequest();
  api::HttpResult multipartPolicy();
  void cleanupRestore();
  void notifyRestoreAssets();
  void sendResponse(int status, const char* contentType, const std::string& body);
  void sendJson(int status, const std::string& body);
  void sendResult(const api::HttpResult& res);
  void reportError(const Request& req, const api::HttpResult& res);
  void sendError(int status, const char* code, const char* message);
  void sendUnauthorized();
  void addCorsHeaders(bool preflight);
  void addCorsHeaders(std::string_view method, std::string_view path);
  bool fromOtherSite() const;
  // Carried across the per-chunk upload callbacks, which cannot answer the client; the matching
  // handle*Done() reads them once the body is in and picks the status code.
  bool uploadWriteOk_ = false;
  bool uploadContentChecked_ = false;
  api::SingleUploadRequest updateRequest_;
  std::string updateImageError_;
  // The image marker straddles chunk boundaries as readily as it sits inside one, so the match runs
  // a byte at a time and its progress lives here between chunks.
  uint8_t markerMatched_ = 0;
  bool markerCapturing_ = false;
  bool markerRead_ = false;
  std::string markerVariant_;
  File uploadFile_;
  api::asset::UploadSession fileUpload_;
  bool multipartRequestActive_ = false;
  BodyArena bodyArena_;
  BodyArena sourceArena_;
  // Growth ceiling for sourceArena_, measured once at RAW_START; takeBody() reports this rather
  // than re-measuring a heap the upload has already spent.
  std::size_t sourceCeiling_ = 0;
  bool restoreAssetsNotified_ = false;
  api::SingleUploadRequest restoreRequest_;
  std::unique_ptr<backup::RestoreSink> restoreSink_;
  std::unique_ptr<backup::RestoreApplier> restoreApplier_;
  std::unique_ptr<backup::ZipReader> restoreReader_;

  WebServer* server_ = nullptr;
  CoreEngine* engine_ = nullptr;
  IBoard* board_ = nullptr;
  Canvas* screen_ = nullptr;
  DeviceConfig* cfg_ = nullptr;
  std::function<void()> onConfigChanged_;
  std::string uid_;
  std::shared_ptr<const std::string> capabilitiesJson_ = std::make_shared<const std::string>("{}");
  DeviceCapabilities deviceCapabilities_{};
  std::function<void()> onAssetsChanged_;
  std::function<void(const std::string& event)> onError_;
  std::function<void(const std::string& path)> releaseFiles_;
  const script::ScriptHost* scripts_ = nullptr;
  ScriptSourceFn scriptSource_;
  ScriptSourceFn scriptStore_;
  StoredScriptsFn storedScripts_;
  std::string respBuf_;
  // A started Wi-Fi scan counts as running for this long, whatever scanComplete() reports.
  static constexpr uint32_t kWifiScanMs = 15000;
  uint32_t wifiScanStartedMs_ = 0;
  bool apMode_ = false;
};

}
