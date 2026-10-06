#pragma once

#include "platform/linux/host/HostHttpServer.h"
#include "core/api/FilesApi.h"
#include "platform/linux/host/vendor/httplib.h"

#include <chrono>
#include <condition_variable>
#include <mutex>
#include <thread>

namespace awtrix {
namespace host_http { class ConnectionTrackingServer; }

struct HostHttpServer::Impl {
  std::unique_ptr<httplib::Server> svr;
  host_http::ConnectionTrackingServer* connections = nullptr;
  std::thread listener;
  uint16_t port = 8080;

  std::mutex m;
  std::condition_variable cv;
  std::vector<std::function<void()>> jobs;
  bool stopping = false;

  CoreEngine* engine = nullptr;
  HostHttpOptions options;
  Canvas* screen = nullptr;
  DeviceConfig* cfg = nullptr;
  std::string uid;
  std::string webuiFile;
  bool webuiGzip = false;
  std::string capabilitiesJson = "{}";
  DeviceCapabilities deviceCapabilities{};
  std::function<void()> onConfigChanged;
  std::function<void()> onAssetsChanged;
  std::function<void(const std::string& event)> onError;
  std::function<void(const std::string& path)> releaseFiles;
  const script::ScriptHost* scripts = nullptr;
  HostHttpServer::ScriptSourceFn scriptSource;
  HostHttpServer::ScriptSourceFn scriptStore;
  HostHttpServer::StoredScriptsFn storedScripts;

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

  bool largeBodyBusy = false;

  bool beginLargeBody() {
    std::unique_lock<std::mutex> lk(m);
    if (!cv.wait_for(lk, std::chrono::seconds(30), [&] { return !largeBodyBusy || stopping; }) || stopping)
      return false;
    largeBodyBusy = true;
    return true;
  }

  void endLargeBody() {
    std::lock_guard<std::mutex> g(m);
    largeBodyBusy = false;
    cv.notify_all();
  }

  void route(const httplib::Request& req, httplib::Response& res);
  bool serveCommand(const httplib::Request& req, const std::string& method, httplib::Response& res);
  void reportError(const httplib::Request& req, const std::string& method, int status,
                   const std::string& body);
  bool serveState(const httplib::Request& req, const std::string& method, httplib::Response& res);
  api::HttpResult uploadParts(const httplib::Request& req, const api::UploadTargetFn& target, api::Files& files);
  void handleRestore(const httplib::Request& req, const std::string& method, httplib::Response& res);
};

}
