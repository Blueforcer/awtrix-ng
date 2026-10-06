#include "core/api/AssetUploadSession.h"

#include <utility>

namespace awtrix::api::asset {

void UploadSession::discard() {
  if (active_ && storage_.discard) storage_.discard();
  if (!directory_.empty() && storage_.prune) storage_.prune(directory_);
  directory_.clear();
  active_ = false;
}

void UploadSession::reset(StreamStorage storage) {
  discard();
  storage_ = std::move(storage);
  validator_.reset({});
  path_.clear();
  failure_ = {};
  known_ = true;
  authorized_ = false;
  failed_ = false;
  received_ = false;
  active_ = false;
}

void UploadSession::fail(HttpResult result) {
  discard();
  active_ = false;
  if (!failed_) failure_ = std::move(result);
  failed_ = true;
}

void UploadSession::authorize(const HttpResult& policy) {
  if (!known_ || failed_) return;
  if (policy.status != 200) { fail(policy); return; }
  authorized_ = true;
}

void UploadSession::start(const std::string& directory, const std::string& filename) {
  start(prepareUpload(directory, filename));
}

void UploadSession::start(const UploadTarget& target) {
  if (failed_) return;
  if (!known_ || !authorized_) { fail(errorResult(400, "badRequest", "not authorized")); return; }
  if (active_) { fail(errorResult(400, "badRequest", "previous file incomplete")); return; }
  received_ = true;
  if (!target.ok()) { fail(target.result); return; }
  HttpResult claimed = claimName(target.path, storage_.exists);
  if (claimed.status != 200) { fail(claimed); return; }
  path_ = target.path;
  validator_.reset(path_);
  active_ = true;
  const auto slash = path_.rfind('/');
  if (storage_.mkdir && slash != std::string::npos && slash > 0) {
    const std::string parent = path_.substr(0, slash);
    bool created = false;
    const bool ready = storage_.mkdir(parent, created);
    if (created) directory_ = parent;
    if (!ready) { fail(finishUpload(path_, true, false)); return; }
  }
  if (!storage_.begin || !storage_.begin(path_)) {
    fail(finishUpload(path_, true, false));
  }
}

void UploadSession::append(const uint8_t* data, std::size_t size) {
  if (failed_) return;
  if (!active_) { fail(errorResult(400, "badRequest", "no file received")); return; }
  if (!validator_.append(data, size)) {
    fail(finishUpload(path_, false, true));
  } else if (size && (!storage_.write || !storage_.write(data, size))) {
    fail(finishUpload(path_, true, false));
  }
}

void UploadSession::end() {
  if (failed_) return;
  if (!active_) { fail(errorResult(400, "badRequest", "no file received")); return; }
  if (!validator_.finish()) { fail(finishUpload(path_, false, true)); return; }
  if (!storage_.publish || !storage_.publish(path_)) {
    fail(finishUpload(path_, true, false));
    return;
  }
  active_ = false;
  directory_.clear();
  if (storage_.changed) storage_.changed();
}

void UploadSession::abort() { fail(errorResult(400, "badRequest", "upload interrupted")); }

HttpResult UploadSession::complete() {
  if (active_) fail(errorResult(400, "badRequest", "upload incomplete"));
  const bool accepted = known_ && authorized_ && received_;
  known_ = false;
  authorized_ = false;
  if (failed_) return failure_;
  if (!accepted) return errorResult(400, "badRequest", "no file received");
  return HttpResult{200, "application/json", "{\"ok\":true}"};
}

}
