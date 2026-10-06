#pragma once

#include "core/api/AssetApi.h"

namespace awtrix::api {

using UploadTargetFn = std::function<asset::UploadTarget(const std::string& filename)>;

// Borrowed for one authenticated request on the application loop.
struct Request {
  const std::string& method;
  const std::string& path;
  const std::string& body;
  std::function<bool(const char* name, std::string& value)> query;
  std::function<HttpResult(const UploadTargetFn&)> upload;
  std::string ifNoneMatch;

  std::string parameter(const char* name, const char* fallback = "") const;
};

class Reply {
 public:
  virtual ~Reply() = default;
  virtual void send(const HttpResult& result, bool streaming = false) = 0;
  virtual void header(const char* name, const std::string& value) = 0;
  // An empty chunk finishes a streaming reply.
  virtual void chunk(const char* data, std::size_t size) = 0;
  virtual bool sendFile(const std::string& path, const char* contentType) = 0;
};

}
