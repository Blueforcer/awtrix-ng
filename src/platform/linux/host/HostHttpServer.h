#pragma once

#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <vector>

#include "core/DeviceCapabilities.h"
#include "core/script/ScriptMeta.h"
#include "platform/linux/host/HostStore.h"

namespace httplib { class Server; struct Request; struct Response; class ContentReader; }

namespace awtrix::script {
class ScriptHost;
}

namespace awtrix {

class CoreEngine;
class Canvas;

struct DeviceConfig;

struct HostHttpOptions {
  std::string listenAddress = "127.0.0.1";
  bool allowCrossOrigin = false;
  // Each request body, an upload included, is held in memory whole.
  uint64_t maxBodyBytes = host::kFsTotalBytes;
  // Platform-owned listener and guard; defaults to the local HTTP service.
  // The guard runs before every route and body read.
  std::function<std::unique_ptr<httplib::Server>()> listenerFactory;
  std::function<bool(const httplib::Request&, httplib::Response&)> requestGuard;
  // LAN service as on the device: any Host, the device's cross-origin policy and its own login.
  std::function<bool(const httplib::Request&, httplib::Response&)> loginGuard;
  // Runs on the loop for an accepted system configuration, before it is persisted.
  std::function<bool(const httplib::Request&, httplib::Response&)> routeGuard;
  std::function<void(DeviceConfig&)> configAccepted;
  // Answers GET /api/v1/system/wifi-scan (status, JSON body); without it the scan is always empty.
  std::function<int(std::string&)> wifiScan;
  std::function<std::string(bool)> deviceState;
  // Takes POST /update on the listener thread, outside the loop and maxBodyBytes, for a request
  // body of at most updateUploadBytes. Without it /update answers 501.
  std::function<void(const httplib::Request&, httplib::Response&, const httplib::ContentReader&)> updateUpload;
  uint64_t updateUploadBytes = 0;
  // Routes only this platform has, asked before the final 404 with the request body: the HTTP
  // status and a JSON body, or 0 for a path that is not its own.
  std::function<int(const std::string& method, const std::string& path, const std::string& request,
                    std::string& body)> platformRoute;
  // Platform-owned pages and configuration, after the common authentication guards.
  std::function<bool(const httplib::Request&, httplib::Response&)> platformHandler;
};

class HostHttpServer {
 public:
  HostHttpServer();
  ~HostHttpServer();

  bool begin(uint16_t port, CoreEngine& engine, Canvas& screen,
             const std::string& uid, DeviceConfig& cfg, const std::string& webuiFile, HostHttpOptions options = {});
  void setCapabilitiesJson(std::string j);
  void setDeviceCapabilities(const DeviceCapabilities& caps);
  void setOnConfigChanged(std::function<void()> cb);
  void setOnAssetsChanged(std::function<void()> cb);
  // Called with api::errorEvent() output for every rejected command.
  void setOnError(std::function<void(const std::string& event)> cb);
  // Asked with a path before a sound there is deleted, so nothing still plays from it.
  void setReleaseFiles(std::function<void(const std::string& path)> release);
  using ScriptSourceFn = std::function<bool(const std::string& name, std::string& out)>;
  using StoredScriptsFn = std::function<std::vector<script::StoredScript>()>;
  void setScripts(const script::ScriptHost* host, ScriptSourceFn readSource,
                  ScriptSourceFn readStore, StoredScriptsFn stored = nullptr);
  // Runs the request handlers the listener thread parked. Must be called from the main loop: it is
  // what lets routes touch the engine without any locking.
  void tick();
  void stop();

 private:
  struct Impl;
  std::unique_ptr<Impl> impl_;
};

}
