#pragma once

#include <cstddef>
#include <cstdint>
#include <string_view>

#include "core/payload/Base64.h"

namespace awtrix::icons {

enum class ImageFormat : uint8_t { kGif, kJpeg };

inline constexpr std::size_t kMaxNameBytes = 64;
inline constexpr std::size_t kMaxInlineBytes = 96 * 1024;
inline constexpr std::size_t kMaxUrlBytes = 2048;

// An icon is named by a file in /ICONS, carried inline as an RFC 2397 data URI, or fetched from
// an http(s) URL. A data URI's media type decides the decoder, never the content; a fetched
// picture is judged by its content alone.
struct Source {
  enum class Kind : uint8_t { kInvalid, kFile, kInline, kRemote };

  Kind kind = Kind::kInvalid;
  std::string_view value;
  ImageFormat format = ImageFormat::kGif;

  bool file() const { return kind == Kind::kFile; }
  bool inlined() const { return kind == Kind::kInline; }
  bool remote() const { return kind == Kind::kRemote; }
  bool offers(ImageFormat wanted) const {
    return kind == Kind::kFile || (kind == Kind::kInline && format == wanted);
  }
};

namespace detail {

inline bool startsWithNoCase(std::string_view text, std::string_view prefix) {
  if (text.size() < prefix.size()) return false;
  for (std::size_t i = 0; i < prefix.size(); ++i) {
    char c = text[i];
    if (c >= 'A' && c <= 'Z') c = static_cast<char>(c - 'A' + 'a');
    if (c != prefix[i]) return false;
  }
  return true;
}

inline bool safeName(std::string_view name) {
  if (name.empty() || name.size() > kMaxNameBytes) return false;
  for (const char c : name)
    if (c == '\0' || c == '/' || c == '\\') return false;
  return name.find("..") == std::string_view::npos;
}

struct DataPrefix {
  std::string_view text;
  ImageFormat format;
};

inline constexpr DataPrefix kDataPrefixes[] = {
    {"data:image/gif;base64,", ImageFormat::kGif},
    {"data:image/jpeg;base64,", ImageFormat::kJpeg},
};

// An http(s) URL of printable ASCII without spaces, with a host after the scheme: anything else
// has to arrive percent-encoded.
[[gnu::noinline]] inline bool url(std::string_view text) {
  const std::size_t scheme = startsWithNoCase(text, "http://") ? 7 : startsWithNoCase(text, "https://") ? 8 : 0;
  if (!scheme || text.size() > kMaxUrlBytes || text.size() == scheme) return false;
  const char first = text[scheme];
  if (first == '/' || first == '?' || first == '#') return false;
  for (const char c : text)
    if (c <= ' ' || c > '~') return false;
  return true;
}

}

// A malformed URL is no name either: names cannot hold a slash.
inline Source parse(std::string_view text) {
  if (detail::url(text)) return {Source::Kind::kRemote, text, ImageFormat::kGif};
  if (detail::startsWithNoCase(text, "data:")) {
    if (text.size() > kMaxInlineBytes) return {};
    for (const auto& prefix : detail::kDataPrefixes) {
      if (!detail::startsWithNoCase(text, prefix.text)) continue;
      const std::string_view payload = text.substr(prefix.text.size());
      if (payload.empty()) return {};
      return {Source::Kind::kInline, payload, prefix.format};
    }
    return {};
  }
  if (!detail::safeName(text)) return {};
  return {Source::Kind::kFile, text, ImageFormat::kGif};
}

inline bool valid(std::string_view text) {
  const Source source = parse(text);
  if (source.file() || source.remote()) return true;
  return source.inlined() && base64::valid(source.value.data(), source.value.size()) &&
         base64::decodedSize(source.value.data(), source.value.size()) > 0;
}

inline constexpr const char* kInvalidMessage =
    "invalid icon";

}
