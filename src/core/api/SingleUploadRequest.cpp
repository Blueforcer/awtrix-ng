#include "core/api/SingleUploadRequest.h"

#include <utility>

namespace awtrix::api {

void SingleUploadRequest::reset(std::function<void()> cancel) {
  if (resourceLive_ && cancel_) cancel_();
  cancel_ = std::move(cancel);
  failure_ = {};
  known_ = true;
  started_ = false;
  ended_ = false;
  failed_ = false;
  startAuthorized_ = false;
  resourceLive_ = false;
}

void SingleUploadRequest::fail(const HttpResult& result) {
  if (resourceLive_ && cancel_) cancel_();
  resourceLive_ = false;
  if (!failed_) failure_ = result;
  failed_ = true;
}

bool SingleUploadRequest::start(const HttpResult& policy) {
  if (failed_) return false;
  if (!known_) { fail(errorResult(400, "badRequest", "no upload")); return false; }
  if (started_) { fail(errorResult(400, "badRequest", "send exactly one file")); return false; }
  started_ = true;
  if (policy.status != 200) { fail(policy); return false; }
  startAuthorized_ = true;
  resourceLive_ = true;
  return true;
}

void SingleUploadRequest::end() {
  if (failed_) return;
  if (!accepting()) { fail(errorResult(400, "badRequest", "no active upload")); return; }
  ended_ = true;
}

void SingleUploadRequest::abort() { fail(errorResult(400, "badRequest", "upload interrupted")); }

HttpResult SingleUploadRequest::complete(const HttpResult& currentPolicy,
                                         CompletionAuthorization authorization,
                                         const std::function<HttpResult()>& finalize) {
  // Restore may change the credentials it was authenticated with while parsing the ZIP.
  // Start authorization is usable only for the one selected request that actually began;
  // an empty request or a subsequent request must pass its own current policy check.
  const bool useStart = authorization == CompletionAuthorization::Start &&
                        known_ && started_ && startAuthorized_;
  if (!useStart && currentPolicy.status != 200) {
    fail(currentPolicy);
    known_ = false;
    return currentPolicy;
  }
  if (!failed_ && (!known_ || !started_)) fail(errorResult(400, "badRequest", "no file received"));
  if (!failed_ && !ended_) fail(errorResult(400, "badRequest", "upload incomplete"));
  known_ = false;
  if (failed_) return failure_;
  // Firmware finalization can select the next boot partition. It must happen after the
  // entire request and its current authorization pass, while cancellation is still live.
  if (finalize) {
    const HttpResult result = finalize();
    if (result.status != 200) { fail(result); return failure_; }
  }
  resourceLive_ = false;
  return HttpResult{200, "application/json", "{\"ok\":true}"};
}

}
