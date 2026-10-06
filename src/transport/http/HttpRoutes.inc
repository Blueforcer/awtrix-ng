bool HttpApiServer::authOk() {
  if (!cfg_->authEnabled) return true;
  if (server_->authenticate(cfg_->authUser.c_str(), cfg_->authPass.c_str())) return true;
  sendUnauthorized();
  return false;
}

void HttpApiServer::sendUnauthorized() {
  server_->sendHeader("WWW-Authenticate", api::kAuthenticationChallenge);
  sendResult(api::unauthorized());
}

void HttpApiServer::sendResponse(int status, const char* contentType, const std::string& body) {
  static_cast<RawWebServer*>(server_)->sendBody(status, contentType, body);
}

// The probe:: calls bracket heap-accounting windows and compile away unless AWTRIX_HEAP_PROBE is
// defined; they are sprinkled through the request path to attribute allocations to a phase.
void HttpApiServer::sendJson(int status, const std::string& body) {
  probe::report("resp:build", 128);
  probe::begin();
  sendResponse(status, "application/json", body);
  probe::report("resp:send", 128);
  probe::begin();
}

void HttpApiServer::sendResult(const api::HttpResult& res) {
  if (res.retryAfterSeconds > 0)
    server_->sendHeader("Retry-After", String(res.retryAfterSeconds));
  sendResponse(res.status, res.contentType, res.body);
}

void HttpApiServer::sendError(int status, const char* code, const char* message) {
  sendJson(status, api::errorJson(code, message));
}

void HttpApiServer::addCorsHeaders(bool preflight) {
  api::corsHeaders([this](const char* name, const char* value) { server_->sendHeader(name, value); }, preflight);
}

void HttpApiServer::addCorsHeaders(std::string_view method, std::string_view path) {
  if (!api::ownPageOnly(method, path, server_->hasArg("secrets"))) addCorsHeaders(false);
}

bool HttpApiServer::fromOtherSite() const {
  const String origin = server_->header("Origin");
  const String host = server_->header("Host");
  const String site = server_->header("Sec-Fetch-Site");
  return api::fromOtherSite({origin.c_str(), origin.length() ? 1u : 0u, host.c_str(),
                             host.length() ? 1u : 0u, site.c_str(), site.length() ? 1u : 0u});
}

void HttpApiServer::dispatch() {
  Request req;
  req.path = server_->uri().c_str();
  const api::MethodResolution resolved = api::resolveHttpMethod(methodName(server_->method()), req.path,
      server_->header(api::kMethodOverrideHeader).c_str());
  req.method = resolved.method;
  req.get = (req.method == "GET");
  if (rejectedByProvisioning(req, !resolved.error)) return;

  if (server_->method() == HTTP_OPTIONS) {
    addCorsHeaders(true);
    dropRawBody();
    server_->send(204, "text/plain", "");
    return;
  }
  addCorsHeaders(req.method, req.path);

  if (!authOk()) {
    dropRawBody();
    return;
  }

  if (resolved.error) {
    dropRawBody();
    sendError(400, "invalidMethodOverride", resolved.error);
    return;
  }
  if (api::ownPageOnly(req.method, req.path, server_->hasArg("secrets")) && fromOtherSite()) {
    dropRawBody();
    sendResult(api::forbiddenOrigin());
    return;
  }
  if (takeBody(req)) return;
  if (rejectedByPolicy(req)) return;

  if (serveWebUi(req)) return;
  if (serveFiles(req)) return;

  probe::report("req:body", 128);
  probe::begin();

  if (serveCommand(req)) return;
  if (serveState(req)) return;
  if (serveDiagnostics(req)) return;
  if (serveSystem(req)) return;

  sendError(404, "notFound", "unknown route");
}

provisioning::Verdict HttpApiServer::provisioningVerdict(std::string_view method,
    std::string_view path, std::string_view body, bool methodValid) const {
  const String host = server_->header("Host");
  const String origin = server_->header("Origin");
  const String apIp = WiFi.softAPIP().toString();
  const String apOrigin = String("http://") + apIp;
  const bool hostValid = provisioning::matchesAuthority(host.c_str(), apIp.c_str(), true);
  const bool originValid = (!origin.length() ||
      provisioning::matchesAuthority(origin.c_str(), apOrigin.c_str(), true)) &&
      server_->header("Sec-Fetch-Site") != "cross-site";
  return provisioning::admit({method, path, body, hostValid, originValid,
      server_->hasArg("secrets"), methodValid});
}

bool HttpApiServer::rejectedByProvisioning(const Request& req, bool methodValid) {
  if (!apMode_) return false;
  const auto verdict = provisioningVerdict(req.method, req.path, req.body, methodValid);
  if (verdict == provisioning::Verdict::Allow) return false;
  dropRawBody();
  server_->sendHeader("Connection", "close");
  if (verdict == provisioning::Verdict::Redirect) {
    server_->sendHeader("Location", String("http://") + WiFi.softAPIP().toString() + "/", true);
    server_->send(302, "text/plain", "");
  } else {
    sendError(403, "forbidden", provisioning::kRefusal);
  }
  return true;
}

// Moves whatever the arena collected into req.body. Returns true when it already answered the
// client, in which case the caller must stop.
bool HttpApiServer::takeBody(Request& req) {
  const bool rawSource = api::isRawBodyWrite(req.method, req.path);
  BodyArena& arena = rawSource ? sourceArena_ : bodyArena_;
  // Which reading is honest depends on how the body arrived. One the arena took charge of had
  // its ceiling taken at RAW_START, which is what leaves the arena in any state but Idle. Idle
  // means the arena collected nothing for this body - it declared no length, so there was
  // nothing to receive - and a stored reading from an earlier request would not be its own.
  // Both readings are taken before this handler makes its own copy.
  const std::size_t sourceCeiling =
      rawSource ? sourceCeilingFor(arena.state() != BodyArena::State::Idle, sourceCeiling_,
                                   script::heap::growthBudget())
                : 0;

  // Overflow is the one state both ways of not fitting end in: a declared length the arena would
  // not allocate for at all, and a body that overran the buffer it did get.
  if (arena.state() == BodyArena::State::Overflow) {
    arena.reset();
    if (rawSource) {
      arena.release();
      sendJson(507, api::errorJson("insufficientStorage", sourceTooLargeMessage(sourceCeiling),
                                   "source"));
    } else {
      const std::string msg = "body exceeds " + std::to_string(kMaxBodyBytes) + " bytes";
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
          heap_caps_get_largest_free_block(scriptBufferHeapCaps()) <
              received.size() + kBodyCopyMarginBytes) {
        arena.reset();
        if (rawSource) arena.release();
        sendError(507, "insufficientStorage",
                  "not enough memory");
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
    if (rawSource) {
      if (req.body.size() > sourceCeiling) {
        sendJson(507, api::errorJson("insufficientStorage", sourceTooLargeMessage(sourceCeiling),
                                     "source"));
        return true;
      }
    } else if (req.body.size() > kMaxBodyBytes) {
      const std::string msg = "body exceeds " + std::to_string(kMaxBodyBytes) + " bytes";
      sendError(413, "payloadTooLarge", msg.c_str());
      return true;
    }
  }
  return false;
}

bool HttpApiServer::rejectedByPolicy(const Request& req) {
  if (rejectedByProvisioning(req)) return true;
  if (!api::acceptsBodyContentType(req.method, req.path, server_->header("Content-Type").c_str())) {
    sendError(415, "unsupportedMediaType", "expected application/json");
    return true;
  }
  return false;
}

// The web UI is a gzip blob in flash, sent verbatim without decompressing. The ETag never changes
// within a build, so a repeat visit costs a 304 instead of the whole transfer.
bool HttpApiServer::serveWebUi(const Request& req) {
  if (req.path != "/" && req.path != "/index.html" && req.path != "/fullscreen") return false;
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

bool HttpApiServer::serveCommand(Request& req) {
  Command cmd;
  api::HttpResult immediate;
  switch (api::routeHttp(req.method, req.path, std::move(req.body), cmd, immediate)) {
    case api::RouteOutcome::Respond:
      probe::report("req:route", 128);
      probe::begin();
      sendJson(immediate.status, immediate.body);
      reportError(req, immediate);
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
      const api::HttpResult out =
          api::commandResponse(*engine_, cmd, r, persistence::commandPending(cmd.type));
      sendResult(out);
      reportError(req, out);
      probe::report("req:resp", 128);
      probe::begin();
      return true;
    }
    case api::RouteOutcome::NoMatch:
    default:
      return false;
  }
}

void HttpApiServer::reportError(const Request& req, const api::HttpResult& res) {
  if (!onError_ || res.status < 400) return;
  const std::string event = api::errorEvent("http", req.method + " " + req.path, res.body);
  if (!event.empty()) onError_(event);
}

bool HttpApiServer::serveState(const Request& req) {
  if (!req.get) return false;
  const api::DeviceStateFn device = [this](bool scripting) {
    return buildDeviceStateJson(*engine_, *board_, uid_, scripting);
  };
  const api::StateReadContext context{*engine_, *screen_, *capabilitiesJson_, scripts_,
                                      scriptSource_, scriptStore_, storedScripts_, device,
                                      &deviceCapabilities_};
  std::string body;
  body.swap(respBuf_);
  const auto result = api::readState(req.method, req.path, context, body);
  if (!result.matched) {
    body.swap(respBuf_);
    return false;
  }
  if (std::strcmp(result.contentType, "application/json") == 0) {
    sendJson(result.status, body);
  } else {
    sendResponse(result.status, result.contentType, body);
  }
  if (result.reuseBuffer) body.swap(respBuf_);
  return true;
}

bool HttpApiServer::serveDiagnostics(const Request& req) {
  EspReply reply(*server_);
  return api::routeDiagnostics(apiRequest(req), reply, [this](api::Reply& response) {
    const int count = WiFi.scanComplete();
    const uint32_t now = millis();
    if (count >= 0) {
      wifiScanStartedMs_ = 0;
    } else if (count == WIFI_SCAN_FAILED &&
               (!wifiScanStartedMs_ || now - wifiScanStartedMs_ >= kWifiScanMs)) {
      WiFi.scanNetworks(true);
      wifiScanStartedMs_ = now ? now : 1;
    }
    if (count < 0) { response.send(api::wifiScanPending()); return; }
    response.send({200, "application/json", {}}, true);
    api::JsonStream output(api::replyChunk, &response);
    api::WifiScanWriter writer(output);
    for (int i = 0; i < count; ++i)
      writer.network(WiFi.SSID(i).c_str(), WiFi.RSSI(i), WiFi.encryptionType(i) != WIFI_AUTH_OPEN);
    writer.end();
    response.chunk("", 0);
    WiFi.scanDelete();
  }, logbuf::streamJsonAfter);
}

bool HttpApiServer::serveSystem(const Request& req) {
  EspReply reply(*server_);
  return api::routeSystemConfig(apiRequest(req), reply, *cfg_, [this] {
    const bool saved = cfg_->save();
    if (onConfigChanged_) onConfigChanged_();
    return saved;
  });
}

api::scriptsounds::InstalledScripts HttpApiServer::installedScripts() const {
  return api::scriptsounds::InstalledScripts(scripts_, scriptSource_);
}

bool HttpApiServer::serveFiles(const Request& req) {
  const auto installed = installedScripts();
  EspFiles files(installed, releaseFiles_, onAssetsChanged_);
  EspReply reply(*server_);
  return api::routeFiles(apiRequest(req), reply, files);
}

api::Request HttpApiServer::apiRequest(const Request& req) const {
  return {req.method, req.path, req.body,
      [this](const char* name, std::string& value) {
        if (!server_->hasArg(name)) return false;
        value = server_->arg(name).c_str();
        return true;
      }, {}, server_->header("If-None-Match").c_str()};
}
