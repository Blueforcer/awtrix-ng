void HttpApiServer::resetMultipartRequest() {
  // Also runs for an empty POST: a prior request cannot donate its authorization or success.
  cleanupRestore();
  restoreRequest_.reset();
  updateRequest_.reset([] { Update.abort(); });
  uploadWriteOk_ = false;
  uploadContentChecked_ = false;
  updateImageError_.clear();
  markerMatched_ = 0;
  markerCapturing_ = false;
  markerRead_ = false;
  markerVariant_.clear();
  restoreAssetsNotified_ = false;
  api::asset::StreamStorage storage;
  storage.exists = [](const std::string& path) { return LittleFS.exists(path.c_str()); };
  storage.mkdir = makeDirectory;
  storage.prune = pruneDirectory;
  storage.begin = [this](const std::string&) {
    uploadFile_ = LittleFS.open(kAssetUploadTemporary, "w");
    return static_cast<bool>(uploadFile_);
  };
  storage.write = [this](const uint8_t* data, std::size_t size) {
    return uploadFile_ && uploadFile_.write(data, size) == size;
  };
  storage.publish = [this](const std::string& path) {
    if (uploadFile_) uploadFile_.close();
    if (releaseFiles_) releaseFiles_(path);
    return LittleFS.rename(kAssetUploadTemporary, path.c_str());
  };
  storage.discard = [this] {
    if (uploadFile_) uploadFile_.close();
    LittleFS.remove(kAssetUploadTemporary);
  };
  storage.changed = onAssetsChanged_;
  fileUpload_.reset(std::move(storage));
  multipartRequestActive_ = true;
}

api::HttpResult HttpApiServer::multipartPolicy() {
  const auto resolved = api::resolveHttpMethod(methodName(server_->method()), server_->uri().c_str(),
      server_->header(api::kMethodOverrideHeader).c_str());
  if (apMode_ && provisioningVerdict(resolved.method, server_->uri().c_str(), {}, !resolved.error) !=
                     provisioning::Verdict::Allow)
    return api::errorResult(403, "forbidden", provisioning::kRefusal);
  if (cfg_->authEnabled &&
      !server_->authenticate(cfg_->authUser.c_str(), cfg_->authPass.c_str()))
    return api::errorResult(401, "unauthorized", "authentication required");
  if (resolved.error) return api::errorResult(400, "invalidMethodOverride", resolved.error);
  if (resolved.method != "POST")
    return api::errorResult(405, "methodNotAllowed", "allowed: POST");
  if (api::ownPageOnly(resolved.method, server_->uri().c_str(), false) && fromOtherSite())
    return api::forbiddenOrigin();
  return {};
}

void HttpApiServer::handleFileUpload() {
  HTTPUpload& upload = server_->upload();
  if (upload.status == UPLOAD_FILE_START) {
    const api::HttpResult policy = multipartPolicy();
    fileUpload_.authorize(policy);
    // Resolve script storage only after admission; a rejected request must do no I/O.
    api::asset::UploadTarget target;
    target.result = policy;
    if (policy.status == 200) {
      const Request request{"POST", server_->uri().c_str(), {}};
      target = api::uploadTarget(apiRequest(request), upload.filename.c_str(), installedScripts());
    }
    fileUpload_.start(target);
  } else if (upload.status == UPLOAD_FILE_WRITE) {
    fileUpload_.append(upload.buf, upload.currentSize);
  } else if (upload.status == UPLOAD_FILE_END) {
    fileUpload_.end();
  } else if (upload.status == UPLOAD_FILE_ABORTED) {
    fileUpload_.abort();
  }
}

void HttpApiServer::handleFileUploadDone() {
  addCorsHeaders("POST", server_->uri().c_str());
  const auto policy = multipartPolicy();
  fileUpload_.authorize(policy);
  const auto result = fileUpload_.complete();
  if (policy.status == 401 || result.status == 401) sendUnauthorized();
  else if (policy.status != 200) sendResult(policy);
  else sendResult(result);
}

void HttpApiServer::notifyRestoreAssets() {
  if (!restoreAssetsNotified_ && restoreApplier_ &&
      api::restoreChangedAssets(restoreApplier_->result())) {
    if (onAssetsChanged_) onAssetsChanged_();
    restoreAssetsNotified_ = true;
  }
}

void HttpApiServer::cleanupRestore() {
  if (restoreApplier_ && !restoreRequest_.ended()) restoreApplier_->abort("upload interrupted");
  // Completed entries can have changed assets even when the remaining ZIP was malformed.
  notifyRestoreAssets();
  restoreReader_.reset();
  restoreApplier_.reset();
  restoreSink_.reset();
}

// Feed the actual ZIP reader while the request streams; no complete backup is buffered.
void HttpApiServer::handleRestoreUpload() {
  HTTPUpload& upload = server_->upload();
  if (upload.status == UPLOAD_FILE_START) {
    if (!restoreRequest_.start(multipartPolicy())) {
      cleanupRestore();
      return;
    }
    restoreSink_.reset(
        new LittleFsRestoreSink(*cfg_, &engine_->state(), onConfigChanged_, releaseFiles_));
    restoreApplier_.reset(new backup::RestoreApplier(*restoreSink_));
    restoreReader_.reset(new backup::ZipReader(*restoreApplier_));
  } else if (upload.status == UPLOAD_FILE_WRITE) {
    if (restoreRequest_.accepting() && restoreReader_)
      restoreReader_->feed(upload.buf, upload.currentSize);
  } else if (upload.status == UPLOAD_FILE_END) {
    if (restoreRequest_.accepting() && restoreReader_) {
      api::finishRestore(*restoreReader_, *restoreApplier_);
      restoreRequest_.end();
      notifyRestoreAssets();
    }
  } else if (upload.status == UPLOAD_FILE_ABORTED) {
    restoreRequest_.abort();
    cleanupRestore();
  }
}

void HttpApiServer::handleRestoreDone() {
  addCorsHeaders("POST", server_->uri().c_str());
  // The archive can replace credentials during parsing. The request that was authorized
  // before that mutation remains authorized; request selection prevents any later reuse.
  auto result = restoreRequest_.complete(multipartPolicy(),
      api::SingleUploadRequest::CompletionAuthorization::Start);
  if (result.status == 200 && restoreApplier_)
    result = api::restoreResponse(restoreApplier_->result(),
                                 (cfg_ && cfg_->persistencePending) || persistence::documentsPending());
  cleanupRestore();
  if (result.status == 401) sendUnauthorized();
  else sendResult(result);
}
