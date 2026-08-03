#include "core/radio/IcyStream.h"

#include <algorithm>
#include <cctype>
#include <cstdlib>

namespace awtrix {
namespace radio {

namespace {

std::string toLower(std::string value) {
  std::transform(value.begin(), value.end(), value.begin(),
                 [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
  return value;
}

std::string trim(const std::string& value) {
  std::size_t begin = 0;
  std::size_t end = value.size();
  while (begin < end && (value[begin] == ' ' || value[begin] == '\t')) ++begin;
  while (end > begin) {
    const char c = value[end - 1];
    if (c != ' ' && c != '\t' && c != '\r') break;
    --end;
  }
  return value.substr(begin, end - begin);
}

}

bool parseUrl(const std::string& url, Url& out) {
  const std::string lower = toLower(url);
  std::size_t offset = 0;
  if (lower.rfind("http://", 0) == 0) {
    out.tls = false;
    out.port = 80;
    offset = 7;
  } else if (lower.rfind("https://", 0) == 0) {
    out.tls = true;
    out.port = 443;
    offset = 8;
  } else {
    return false;
  }

  const std::size_t slash = url.find('/', offset);
  std::string authority =
      slash == std::string::npos ? url.substr(offset) : url.substr(offset, slash - offset);
  out.path = slash == std::string::npos ? "/" : url.substr(slash);
  if (out.path.empty()) out.path = "/";

  // Drop any user:pass@ prefix. We never authenticate, and keeping it would corrupt the host.
  const std::size_t at = authority.find('@');
  if (at != std::string::npos) authority = authority.substr(at + 1);

  const std::size_t colon = authority.rfind(':');
  if (colon != std::string::npos) {
    const std::string portText = authority.substr(colon + 1);
    if (portText.empty()) return false;
    for (char c : portText)
      if (!std::isdigit(static_cast<unsigned char>(c))) return false;
    const long port = std::strtol(portText.c_str(), nullptr, 10);
    if (port <= 0 || port > 65535) return false;
    out.port = static_cast<int>(port);
    authority = authority.substr(0, colon);
  }

  if (authority.empty()) return false;
  out.host = authority;
  return true;
}

std::string resolveRedirect(const Url& base, const std::string& location) {
  if (location.empty()) return std::string();
  const std::string lower = toLower(location);
  if (lower.rfind("http://", 0) == 0 || lower.rfind("https://", 0) == 0) return location;

  std::string out = base.tls ? "https://" : "http://";
  out += base.host;
  const int defaultPort = base.tls ? 443 : 80;
  if (base.port != defaultPort) out += ":" + std::to_string(base.port);
  if (location[0] != '/') out += '/';
  out += location;
  return out;
}

// Reads the server's response head. The status line is taken as "anything, space, number", which
// covers both "HTTP/1.1 200 OK" and the bare "ICY 200 OK" that Shoutcast servers still send.
bool parseResponseHead(const std::string& raw, ResponseHead& out) {
  std::size_t position = 0;
  bool first = true;

  while (position < raw.size()) {
    std::size_t end = raw.find('\n', position);
    if (end == std::string::npos) end = raw.size();
    const std::string line = trim(raw.substr(position, end - position));
    position = end + 1;

    if (first) {
      first = false;
      const std::size_t space = line.find(' ');
      if (space == std::string::npos) return false;
      out.status = static_cast<int>(std::strtol(line.c_str() + space + 1, nullptr, 10));
      if (out.status == 0) return false;
      continue;
    }

    if (line.empty()) break;
    const std::size_t colon = line.find(':');
    if (colon == std::string::npos) continue;
    const std::string key = toLower(trim(line.substr(0, colon)));
    const std::string value = trim(line.substr(colon + 1));

    if (key == "icy-metaint") {
      const long interval = std::strtol(value.c_str(), nullptr, 10);
      if (interval > 0) out.metaInt = static_cast<int>(interval);
    } else if (key == "location") {
      out.location = value;
    } else if (key == "content-type") {
      out.contentType = toLower(value);
    } else if (key == "icy-name") {
      out.stationName = value;
    }
  }
  return out.status != 0;
}

std::string buildRequest(const Url& url, const std::string& userAgent) {
  std::string out = "GET " + url.path + " HTTP/1.1\r\n";
  out += "Host: " + url.host;
  const int defaultPort = url.tls ? 443 : 80;
  if (url.port != defaultPort) out += ":" + std::to_string(url.port);
  out += "\r\n";
  out += "User-Agent: " + userAgent + "\r\n";
  // Icy-MetaData: 1 is what makes the server interleave title blocks and answer with icy-metaint.
  out += "Icy-MetaData: 1\r\n";
  out += "Accept: */*\r\n";
  out += "Connection: close\r\n\r\n";
  return out;
}

void MetadataSplitter::reset(int metaInt) {
  metaInt_ = metaInt > 0 ? metaInt : 0;
  state_ = State::Audio;
  audioLeft_ = metaInt_;
  metadataLeft_ = 0;
  block_.clear();
}

// ICY framing: metaInt bytes of audio, one length byte, then length * 16 bytes of metadata, and
// round again. Socket reads land anywhere in that pattern, so the state carries across calls.
void MetadataSplitter::feed(const uint8_t* data, std::size_t bytes, const AudioSink& audio,
                            const MetadataSink& metadata) {
  if (metaInt_ == 0) {
    if (bytes) audio(data, bytes);
    return;
  }

  std::size_t offset = 0;
  while (offset < bytes) {
    switch (state_) {
      case State::Audio: {
        const std::size_t take =
            std::min(bytes - offset, static_cast<std::size_t>(audioLeft_));
        if (take) audio(data + offset, take);
        offset += take;
        audioLeft_ -= static_cast<int>(take);
        if (audioLeft_ == 0) state_ = State::Length;
        break;
      }
      case State::Length: {
        // A zero length byte means "title unchanged", which is what nearly every block says.
        metadataLeft_ = data[offset] * 16;
        ++offset;
        block_.clear();
        state_ = metadataLeft_ > 0 ? State::Metadata : State::Audio;
        if (state_ == State::Audio) audioLeft_ = metaInt_;
        break;
      }
      case State::Metadata: {
        const std::size_t take =
            std::min(bytes - offset, static_cast<std::size_t>(metadataLeft_));
        block_.append(reinterpret_cast<const char*>(data + offset), take);
        offset += take;
        metadataLeft_ -= static_cast<int>(take);
        if (metadataLeft_ == 0) {
          metadata(block_);
          block_.clear();
          state_ = State::Audio;
          audioLeft_ = metaInt_;
        }
        break;
      }
    }
  }
}

}
}
