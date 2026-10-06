#include "platform/linux/images/HttpPictureLoader.h"

#include "platform/linux/images/PictureDecoder.h"
#include "core/net/Url.h"

namespace awtrix::images {
namespace {

std::string hostOf(const std::string& url) {
  const auto parsed = net::parseUrl(url);
  if (!parsed) return "?";
  std::string host(parsed->host);
  if (!parsed->port.empty()) { host += ':'; host += parsed->port; }
  return host;
}

}

bool HttpPictureLoader::load(const std::string& url, int width, int height, media::RemoteImage& out) {
  std::string body;
  const net::HttpGet::Result fetched = http_.fetch(url, {kMaxBytes, kTimeoutMs, true}, body);
  std::string problem;
  switch (fetched.failure) {
    case net::HttpGet::Failure::None: break;
    case net::HttpGet::Failure::Status: problem = "HTTP " + std::to_string(fetched.status); break;
    case net::HttpGet::Failure::TooLarge:
      problem = "larger than " + std::to_string(kMaxBytes / 1024 / 1024) + " MB";
      break;
    case net::HttpGet::Failure::Network: problem = "no answer"; break;
    case net::HttpGet::Failure::Aborted: return false;
  }
  if (problem.empty()) {
    switch (decodePicture(reinterpret_cast<const uint8_t*>(body.data()), body.size(), width, height, out)) {
      case DecodeFailure::None: return true;
      case DecodeFailure::Format: problem = "not a JPEG, PNG or GIF the display can read"; break;
      case DecodeFailure::TooManyPixels: problem = "too many pixels"; break;
      case DecodeFailure::GifTooLarge:
        problem = "GIF larger than " + std::to_string(width) + "x" + std::to_string(height) + " or " +
                  std::to_string(media::kMaxRemoteGifBytes / 1024) + " KB";
        break;
      case DecodeFailure::Unfit:
        problem = "cannot be fitted to " + std::to_string(width) + "x" + std::to_string(height);
        break;
      case DecodeFailure::Memory: problem = "out of memory"; break;
    }
  }
  if (log_) log_("picture from " + hostOf(url) + ": " + problem);
  return false;
}

}
