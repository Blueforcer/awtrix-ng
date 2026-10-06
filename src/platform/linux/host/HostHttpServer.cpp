#include "platform/linux/host/HostHttpServer.h"

#include <algorithm>
#include <chrono>

#include "core/api/HttpProtocol.h"
#include "system/Log.h"
#include "platform/linux/host/HostHttpInternal.h"
#include "platform/linux/host/HostHttpBody.h"
#include "platform/linux/host/HttpConnections.h"

namespace awtrix {
using namespace host_http;

namespace {

// httplib only ends a connection stalled in a read or write at its timeout. Stopping gives an
// answer under way this long to reach its client, then shuts down every connection still open.
constexpr auto kAnswerGrace = std::chrono::milliseconds(500);

bool ownPageRequest(const httplib::Request& req) {
  const api::MethodResolution resolved = api::resolveHttpMethod(
      req.method, req.path,
      req.has_header(api::kMethodOverrideHeader) ? req.get_header_value(api::kMethodOverrideHeader)
                                                 : std::string());
  return api::ownPageOnly(resolved.error ? req.method : resolved.method, req.path,
                          req.has_param("secrets"));
}

bool fromOtherSite(const httplib::Request& req) {
  const std::string origin = req.get_header_value("Origin");
  const std::string host = req.get_header_value("Host");
  const std::string site = req.get_header_value("Sec-Fetch-Site");
  return api::fromOtherSite({origin, req.get_header_value_count("Origin"), host,
                             req.get_header_value_count("Host"), site,
                             req.get_header_value_count("Sec-Fetch-Site")});
}

}

HostHttpServer::HostHttpServer() : impl_(new Impl) {}

HostHttpServer::~HostHttpServer() { stop(); }

void HostHttpServer::setCapabilitiesJson(std::string j) { impl_->capabilitiesJson = std::move(j); }
void HostHttpServer::setDeviceCapabilities(const DeviceCapabilities& caps) { impl_->deviceCapabilities = caps; }
void HostHttpServer::setOnConfigChanged(std::function<void()> cb) {
  impl_->onConfigChanged = std::move(cb);
}
void HostHttpServer::setOnAssetsChanged(std::function<void()> cb) {
  impl_->onAssetsChanged = std::move(cb);
}
void HostHttpServer::setOnError(std::function<void(const std::string& event)> cb) {
  impl_->onError = std::move(cb);
}
void HostHttpServer::setReleaseFiles(std::function<void(const std::string& path)> release) {
  impl_->releaseFiles = std::move(release);
}
void HostHttpServer::setScripts(const script::ScriptHost* host, ScriptSourceFn readSource,
                               ScriptSourceFn readStore, StoredScriptsFn stored) {
  impl_->scripts = host;
  impl_->scriptSource = std::move(readSource);
  impl_->scriptStore = std::move(readStore);
  impl_->storedScripts = std::move(stored);
}

bool HostHttpServer::begin(uint16_t port, CoreEngine& engine, Canvas& screen,
                          const std::string& uid, DeviceConfig& cfg,
                          const std::string& webuiFile, HostHttpOptions options) {
  Impl* im = impl_.get();
  im->engine = &engine;
  im->options = std::move(options);
  im->screen = &screen;
  im->cfg = &cfg;
  im->uid = uid;
  im->webuiFile = webuiFile;
  im->webuiGzip = webuiFile.size() > 3 && webuiFile.compare(webuiFile.size() - 3, 3, ".gz") == 0;
  im->port = port;
  if (im->options.listenerFactory) {
    im->svr = im->options.listenerFactory();
  } else {
    auto listener = std::make_unique<ConnectionTrackingServer>();
    im->connections = listener.get();
    im->svr = std::move(listener);
  }
  if (!im->svr || !im->svr->is_valid()) {
    logf("host http: listener configuration rejected");
    return false;
  }
  const uint64_t maxBodyBytes = im->options.maxBodyBytes;
  im->svr->set_payload_max_length(im->options.updateUpload ? std::max(maxBodyBytes, im->options.updateUploadBytes)
                                                           : maxBodyBytes);
  im->svr->set_read_timeout(10, 0);
  im->svr->set_write_timeout(10, 0);

  std::function<bool(const httplib::Request&, httplib::Response&)> admit;
  if (im->options.requestGuard) {
    if (im->options.allowCrossOrigin) return false;
    admit = im->options.requestGuard;
  } else if (im->options.loginGuard) {
    if (!im->options.allowCrossOrigin) return false;
    im->svr->set_default_headers({{"Access-Control-Allow-Origin", "*"}});
    admit = im->options.loginGuard;
  } else if (im->options.allowCrossOrigin) {
    im->svr->set_default_headers({{"Access-Control-Allow-Origin", "*"}});
  } else {
    // The development service has no authentication. Constrain browser requests to
    // its local origin, including Host validation against DNS rebinding.
    im->svr->set_default_headers({{"X-Content-Type-Options", "nosniff"}});
    admit = [port](const httplib::Request& req, httplib::Response& res) {
      const auto host = req.get_header_value("Host");
      const auto suffix = ":" + std::to_string(port);
      const auto origin = req.get_header_value("Origin");
      if (req.get_header_value_count("Host") != 1 ||
          (host != "127.0.0.1" + suffix && host != "localhost" + suffix) ||
          (req.has_header("Origin") && origin != "http://" + host) ||
          req.get_header_value("Sec-Fetch-Site") == "cross-site") {
        sendError(res, 403, "forbiddenOrigin", "request must originate from this local service");
        return false;
      }
      return true;
    };
  }
  // httplib reads the body of any other method before dispatch, outside the one-at-a-time rule.
  im->svr->set_pre_routing_handler([admit](const httplib::Request& req,
                                                         httplib::Response& res) {
    const bool ownPage = ownPageRequest(req);
    if (ownPage) res.headers.erase("Access-Control-Allow-Origin");
    if (admit && !admit(req, res)) return httplib::Server::HandlerResponse::Handled;
    if (ownPage && fromOtherSite(req)) {
      res.set_header("Connection", "close");
      const api::HttpResult refusal = api::forbiddenOrigin();
      sendJson(res, refusal.status, refusal.body);
      return httplib::Server::HandlerResponse::Handled;
    }
    if (req.method != "POST" && req.method != "PUT" && req.method != "PATCH" && req.method != "DELETE" &&
        readsAlone(req)) {
      res.set_header("Connection", "close");
      sendError(res, 413, "payloadTooLarge", "only POST, PUT, PATCH and DELETE take a large body");
      return httplib::Server::HandlerResponse::Handled;
    }
    return httplib::Server::HandlerResponse::Unhandled;
  });

  const auto marshal = [im](const httplib::Request& req, httplib::Response& res) {
    im->runOnLoop([&] { im->route(req, res); });
  };
  const auto marshalWithBody = [im, marshal, maxBodyBytes](const httplib::Request& req,
                                                          httplib::Response& res,
                                                          const httplib::ContentReader& content) {
    if (!req.has_header("Transfer-Encoding") && req.get_header_value_u64("Content-Length") > maxBodyBytes) {
      discardBody(req, content);
      refuseBody(res, maxBodyBytes);
      return;
    }
    const bool alone = readsAlone(req);
    if (alone && !im->beginLargeBody()) {
      res.set_header("Retry-After", "2");
      res.set_header("Connection", "close");
      sendError(res, 503, "serviceBusy", "busy, try again");
      return;
    }
    {
      httplib::Request request = req;
      if (readBody(content, request, res, maxBodyBytes)) marshal(request, res);
      else if (res.status == 413) refuseBody(res, maxBodyBytes);
    }
    if (alone) im->endLargeBody();
  };
  im->svr->Get(".*", marshal);
  if (im->options.updateUpload) {
    im->svr->Post("/update", [im](const httplib::Request& req, httplib::Response& res,
                                  const httplib::ContentReader& content) {
      const api::MethodResolution resolved = api::resolveHttpMethod(
          req.method, req.path,
          req.has_header(api::kMethodOverrideHeader) ? req.get_header_value(api::kMethodOverrideHeader)
                                                     : std::string());
      if (resolved.error) {
        sendError(res, 400, "invalidMethodOverride", resolved.error);
      } else if (resolved.method != "POST") {
        sendError(res, 405, "methodNotAllowed", "allowed: POST");
      } else {
        im->options.updateUpload(req, res, content);
        return;
      }
      res.set_header("Connection", "close");
      discardBody(req, content);
    });
  }
  im->svr->Post(".*", marshalWithBody);
  im->svr->Put(".*", marshalWithBody);
  im->svr->Patch(".*", marshalWithBody);
  im->svr->Delete(".*", marshalWithBody);
  im->svr->Options(".*", [im](const httplib::Request&, httplib::Response& res) {
    res.status = 204;
    if (!im->options.allowCrossOrigin) return;
    res.headers.erase("Access-Control-Allow-Origin");
    api::corsHeaders([&res](const char* name, const char* value) { res.set_header(name, value); }, true);
  });

  if (!im->svr->bind_to_port(im->options.listenAddress.c_str(), port)) {
    logf("host http: cannot bind port %u", static_cast<unsigned>(port));
    return false;
  }
  im->listener = std::thread([im] { im->svr->listen_after_bind(); });
  return true;
}

void HostHttpServer::tick() {
  std::vector<std::function<void()>> pending;
  {
    std::lock_guard<std::mutex> g(impl_->m);
    pending.swap(impl_->jobs);
  }
  for (auto& job : pending) job();
}

void HostHttpServer::stop() {
  if (!impl_) return;
  // The stopping flag has to be raised first: it releases any handler still blocked in runOnLoop,
  // which would otherwise wait forever for a tick() that is never coming and deadlock the join.
  {
    std::lock_guard<std::mutex> g(impl_->m);
    impl_->stopping = true;
    impl_->cv.notify_all();
  }
  if (impl_->svr) impl_->svr->stop();
  if (impl_->connections) impl_->connections->disconnectAll(kAnswerGrace);
  if (impl_->listener.joinable()) impl_->listener.join();
}

}
