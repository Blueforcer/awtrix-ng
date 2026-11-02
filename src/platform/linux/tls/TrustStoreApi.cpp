#include "platform/linux/tls/TrustStoreApi.h"

#include "core/api/ApiRouter.h"
#include "core/api/HttpProtocol.h"
#include "core/api/JsonReader.h"
#include "platform/linux/host/vendor/httplib.h"

namespace awtrix::tls {
bool TrustStoreApi::handle(const httplib::Request& req, httplib::Response& res) {
  constexpr char base[] = "/api/v1/tls/trust";
  if (req.path != base && req.path.compare(0, sizeof(base), std::string(base) + '/') != 0) return false;
  res.set_header("Cache-Control", "no-store");
  const auto fail = [&](int status, const char* code, const char* message, const char* field = "") {
    res.status = status;
    res.set_content(api::errorJson(code, message, field), "application/json");
  };
  if (req.path == base && req.method == "GET") {
    res.status = 200;
    res.set_content(store_.status(), "application/json");
    return true;
  }
  if (req.get_header_value("X-Awtrix-Trust") != "1" ||
      !api::sameOrigin(req.get_header_value("Origin"), req.get_header_value("Host"),
                       req.get_header_value_count("Origin"), req.get_header_value_count("Host"), true)) {
    fail(403, "forbiddenOrigin", "only from this device's web page");
    return true;
  }
  std::string host, part;
  if (req.path != base) {
    host = req.path.substr(sizeof(base));
    const auto slash = host.find('/');
    if (slash != host.npos) { part = host.substr(slash + 1); host.resize(slash); }
    host = TrustStore::normalizeHost(host);
    if (host.empty()) { fail(422, "validationFailed", "expected a hostname or IP address", "host"); return true; }
  }
  int status = 405;
  if (req.method == "DELETE") {
    status = req.path == base ? (store_.reset() ? 200 : 507) : store_.remove(host, part);
  } else if ((req.method == "PUT" && part == "ca") || (req.method == "POST" && part == "pin")) {
    if (req.body.size() > 2 * TrustStore::kMaxCaBytes + 1024) {
      fail(413, "payloadTooLarge", "certificate request too large"); return true;
    }
    if (!api::isWellFormed(req.body)) { fail(400, "invalidJson", "invalid JSON"); return true; }
    const auto field = part == "ca" ? "certificate" : "fingerprint";
    std::string value;
    if (!api::memberValue(req.body, field).appendString(value) || value.empty()) {
      fail(422, "validationFailed", "expected a nonempty string", field); return true;
    }
    status = part == "ca" ? store_.uploadCa(host, value) : store_.approve(host, value);
  }
  if (status == 200) {
    res.status = 200;
    if (changed_) changed_(host);
    res.set_content(store_.status(), "application/json");
  } else if (status == 409) fail(status, "conflict", "pending fingerprint changed or trusted host limit reached");
  else if (status == 422) fail(status, "validationFailed", "invalid host, certificate or fingerprint", part == "ca" ? "certificate" : "fingerprint");
  else if (status == 503) fail(status, "trustUnavailable", "reset the unusable certificate store first");
  else if (status == 507) fail(status, "insufficientStorage", "certificate trust not saved");
  else fail(405, "methodNotAllowed", "use GET, PUT ca, POST pin or DELETE");
  return true;
}
}
