#pragma once

#include <functional>

#include "core/api/ApiRouter.h"

namespace awtrix::api {

// Lifecycle for routes that consume one uploaded file. The transport calls reset when
// selecting a new request, before headers exist, then supplies its real policy decision.
class SingleUploadRequest {
 public:
  enum class CompletionAuthorization { Current, Start };
  void reset(std::function<void()> cancel = {});
  bool start(const HttpResult& policy);
  bool accepting() const { return known_ && started_ && !ended_ && !failed_; }
  bool started() const { return started_; }
  bool ended() const { return ended_; }
  void end();
  void fail(const HttpResult& result);
  void abort();
  HttpResult complete(const HttpResult& currentPolicy,
                      CompletionAuthorization authorization = CompletionAuthorization::Current,
                      const std::function<HttpResult()>& finalize = {});

 private:
  std::function<void()> cancel_;
  HttpResult failure_;
  bool known_ = false;
  bool started_ = false;
  bool ended_ = false;
  bool failed_ = false;
  bool startAuthorized_ = false;
  bool resourceLive_ = false;
};

}
