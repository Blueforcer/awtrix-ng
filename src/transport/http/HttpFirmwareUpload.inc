void HttpApiServer::scanImageMarker(const uint8_t* buf, size_t len) {
  for (size_t i = 0; i < len; ++i) {
    const char c = static_cast<char>(buf[i]);
    if (markerCapturing_) {
      // The variant ends at the string terminator. Anything else unprintable ends it too, so a
      // corrupt marker cannot pull the rest of the image into the name.
      if (c > 0x20 && markerVariant_.size() < kImageVariantMaxLen) {
        markerVariant_ += c;
        continue;
      }
      markerRead_ = true;
      return;
    }
    if (c == kImageMarker[markerMatched_]) {
      if (++markerMatched_ == kImageMarkerPrefixLen) {
        markerCapturing_ = true;
        markerVariant_.reserve(kImageVariantMaxLen);
      }
    } else {
      // Restart, but not blindly at zero: the byte that broke the match may open the next one.
      markerMatched_ = (c == kImageMarker[0]) ? 1 : 0;
    }
  }
}

void HttpApiServer::handleUpdateUpload() {
  HTTPUpload& up = server_->upload();
  if (up.status == UPLOAD_FILE_START) {
    if (!updateRequest_.start(multipartPolicy())) return;
    uploadWriteOk_ = false;
    const size_t contentLen = server_->clientContentLength();
    if (contentLen > 0 && contentLen > ESP.getFreeSketchSpace()) {
      updateRequest_.fail(api::errorResult(500, "internalError",
                                        "update failed"));
      return;
    }
    uploadWriteOk_ = Update.begin(UPDATE_SIZE_UNKNOWN);
    if (!uploadWriteOk_)
      updateRequest_.fail(api::errorResult(500, "internalError",
                                        "update failed"));
  } else if (up.status == UPLOAD_FILE_WRITE) {
    if (!updateRequest_.accepting()) return;
    // Catch an image that does not belong in the OTA slot from the very first chunk, before any of
    // it reaches flash.
    if (uploadWriteOk_ && !uploadContentChecked_ && up.currentSize >= kEspImageHeaderBytes) {
      uploadContentChecked_ = true;
      if (up.buf[0] == kEspFlashErasedByte) {
        updateImageError_ = std::string("USB install image; upload ") + kUpdateImageName;
      } else if (up.buf[0] != kEspImageMagic) {
        updateImageError_ = "not a firmware image";
      } else {
        const uint16_t chip = static_cast<uint16_t>(up.buf[kEspImageChipIdOffset]) |
                              static_cast<uint16_t>(up.buf[kEspImageChipIdOffset + 1] << 8);
        const uint32_t appDesc = static_cast<uint32_t>(up.buf[kEspAppDescOffset]) |
                                 static_cast<uint32_t>(up.buf[kEspAppDescOffset + 1]) << 8 |
                                 static_cast<uint32_t>(up.buf[kEspAppDescOffset + 2]) << 16 |
                                 static_cast<uint32_t>(up.buf[kEspAppDescOffset + 3]) << 24;
        if (chip != kExpectedChipId)
          updateImageError_ = std::string("image for ") + chipIdName(chip) + ", device is " +
                              chipIdName(kExpectedChipId);
        else if (appDesc != kEspAppDescMagic)
          updateImageError_ = std::string("USB install image; upload ") + kUpdateImageName;
      }
      if (!updateImageError_.empty()) {
        logf("update: refused - %s", updateImageError_.c_str());
        uploadWriteOk_ = false;
        updateRequest_.fail(api::errorResult(400, "wrongChip", updateImageError_.c_str()));
        return;
      }
    }
    if (uploadWriteOk_ && !markerRead_) {
      scanImageMarker(up.buf, up.currentSize);
      if (markerRead_ && markerVariant_ != kImageVariant) {
        updateImageError_ = std::string("image for ") + variantName(markerVariant_) + ", device is " +
                            variantName(kImageVariant) + "; upload " + kUpdateImageName;
        logf("update: refused - %s", updateImageError_.c_str());
        uploadWriteOk_ = false;
        updateRequest_.fail(api::errorResult(400, "wrongChip", updateImageError_.c_str()));
        return;
      }
    }
    if (uploadWriteOk_ && Update.write(up.buf, up.currentSize) != up.currentSize) {
      uploadWriteOk_ = false;
      updateRequest_.fail(api::errorResult(500, "internalError",
                                        "update failed"));
    }
  } else if (up.status == UPLOAD_FILE_END) {
    if (!updateRequest_.accepting()) return;
    if (uploadWriteOk_) {
      // FILE_END only closes one multipart part. Activation waits for the complete
      // request, so an extra file, parser failure or failed final auth cannot select it.
      updateRequest_.end();
    } else {
      uploadWriteOk_ = false;
      updateRequest_.fail(api::errorResult(500, "internalError",
                                        "update failed"));
    }
  } else if (up.status == UPLOAD_FILE_ABORTED) {
    uploadWriteOk_ = false;
    updateRequest_.abort();
  }
}

void HttpApiServer::handleUpdateDone() {
  addCorsHeaders("POST", server_->uri().c_str());
  // Authenticate this completed request even if no upload callback ran at all.
  const auto result = updateRequest_.complete(multipartPolicy(),
      api::SingleUploadRequest::CompletionAuthorization::Current, [this] {
    if (!uploadWriteOk_ || Update.hasError() || !Update.end(true)) {
      uploadWriteOk_ = false;
      return api::errorResult(500, "internalError",
                            "update failed");
    }
    return api::HttpResult{};
  });
  if (result.status != 200) {
    if (result.status == 401) sendUnauthorized();
    else sendResult(result);
    return;
  }
  sendJson(200, "{\"ok\":true}");
  engine_->execute(Command(CommandType::Reboot));
}
