#pragma once

#include <cstddef>
#include <cstdint>
#include <functional>
#include <string>
#include <string_view>

namespace awtrix::api {

struct HttpResult;

class Etag {
 public:
  void append(const void* data, std::size_t size);
  std::string value() const;

 private:
  uint32_t hash_ = 2166136261u;
  std::size_t size_ = 0;
};

bool isUploadRoute(std::string_view path);

using HeaderFn = std::function<void(const char* name, const char* value)>;
void corsHeaders(const HeaderFn& header, bool preflight);

// The request headers a browser uses to name the page that sent a request, with how often each
// arrived.
struct BrowserOrigin {
  std::string_view origin;
  std::size_t originCount = 0;
  std::string_view host;
  std::size_t hostCount = 0;
  std::string_view fetchSite;
  std::size_t fetchSiteCount = 0;
};

// True for a request from a page of another site: an Origin that does not name Host, a
// Sec-Fetch-Site other than same-origin or none, or a repeated header.
bool fromOtherSite(const BrowserOrigin& request);

// Routes that read, replace or erase the device's credentials or firmware. Their answers carry no
// CORS headers, and a request from a page of another site gets forbiddenOrigin().
bool ownPageOnly(std::string_view method, std::string_view path, bool secrets);
HttpResult forbiddenOrigin();

}
