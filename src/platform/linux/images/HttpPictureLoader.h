#pragma once

#include <cstddef>
#include <cstdint>
#include <functional>
#include <string>

#include "platform/linux/images/RemoteImageStore.h"
#include "platform/linux/net/HttpGet.h"

namespace awtrix::images {

// Downloads a picture over http(s), redirects followed, and fits it. A failure is logged with
// the host only, as the rest of a URL may carry a token.
class HttpPictureLoader final : public IPictureLoader {
 public:
  static constexpr std::size_t kMaxBytes = 1024 * 1024;
  static constexpr int64_t kTimeoutMs = 15000;

  explicit HttpPictureLoader(std::function<void(const std::string&)> log) : log_(std::move(log)) {}
  bool load(const std::string& url, int width, int height, media::RemoteImage& out) override;
  void abort() override { http_.abort(); }

 private:
  net::HttpGet http_;
  std::function<void(const std::string&)> log_;
};

}
