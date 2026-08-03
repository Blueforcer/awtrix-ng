#pragma once

#include <cstddef>
#include <cstdint>
#include <functional>
#include <string>

namespace awtrix {
namespace radio {


struct Url {
  std::string host;
  std::string path = "/";
  int port = 80;
  bool tls = false;
};

bool parseUrl(const std::string& url, Url& out);

std::string resolveRedirect(const Url& base, const std::string& location);

struct ResponseHead {
  int status = 0;
  int metaInt = 0;
  std::string location;
  std::string contentType;
  std::string stationName;
};

bool parseResponseHead(const std::string& raw, ResponseHead& out);

std::string buildRequest(const Url& url, const std::string& userAgent);

class MetadataSplitter {
 public:
  using AudioSink = std::function<void(const uint8_t* data, std::size_t bytes)>;
  using MetadataSink = std::function<void(const std::string& block)>;

  void reset(int metaInt);

  void feed(const uint8_t* data, std::size_t bytes, const AudioSink& audio,
            const MetadataSink& metadata);

 private:
  enum class State : uint8_t { Audio, Length, Metadata };

  int metaInt_ = 0;
  State state_ = State::Audio;
  int audioLeft_ = 0;
  int metadataLeft_ = 0;
  std::string block_;
};

}
}
