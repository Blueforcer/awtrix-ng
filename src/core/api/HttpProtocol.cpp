#include "core/api/HttpProtocol.h"

#include <cstdio>

#include "core/api/ApiRouter.h"
#include "core/api/ScriptSoundsApi.h"

namespace awtrix::api {

void Etag::append(const void* data, std::size_t size) {
  const auto* bytes = static_cast<const uint8_t*>(data);
  for (std::size_t i = 0; i < size; ++i) {
    hash_ ^= bytes[i];
    hash_ *= 16777619u;
  }
  size_ += size;
}

std::string Etag::value() const {
  char tag[40];
  std::snprintf(tag, sizeof(tag), "\"%zx-%lx\"", size_, static_cast<unsigned long>(hash_));
  return tag;
}

bool isUploadRoute(std::string_view path) {
  return path == "/api/v1/files" || path == "/api/v1/audio/mp3" ||
         path == "/api/v1/restore" || path == "/update" || scriptsounds::isUpload(path);
}

void corsHeaders(const HeaderFn& header, bool preflight) {
  header("Access-Control-Allow-Origin", "*");
  if (!preflight) return;
  header("Access-Control-Allow-Methods", "GET, POST, PUT, PATCH, DELETE, OPTIONS");
  const std::string allowed = std::string("Content-Type, Authorization, ") + kMethodOverrideHeader;
  header("Access-Control-Allow-Headers", allowed.c_str());
  header("Access-Control-Allow-Private-Network", "true");
  header("Access-Control-Max-Age", "600");
}

bool fromOtherSite(const BrowserOrigin& request) {
  if (request.fetchSiteCount > 1 ||
      (request.fetchSiteCount == 1 && request.fetchSite != "same-origin" && request.fetchSite != "none"))
    return true;
  return !sameOrigin(request.origin, request.host, request.originCount, request.hostCount);
}

bool ownPageOnly(std::string_view method, std::string_view path, bool secrets) {
  if (path == "/api/v1/system") return method == "PUT" || (method == "GET" && secrets);
  return method == "POST" &&
         (path == "/update" || path == "/api/v1/restore" || path == "/api/v1/device/factory-reset");
}

HttpResult forbiddenOrigin() {
  return errorResult(403, "forbiddenOrigin", "only from this device's web page");
}

}
