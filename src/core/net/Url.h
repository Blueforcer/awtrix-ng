#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>

namespace awtrix::net {

namespace url_detail {
inline bool ipv6(std::string_view address) {
  unsigned groups = 0;
  bool compressed = false;
  if (address.substr(0, 2) == "::") {
    compressed = true;
    address.remove_prefix(2);
  }
  while (!address.empty()) {
    const auto colon = address.find(':');
    const auto part = address.substr(0, colon);
    if (part.empty()) return false;
    if (part.find('.') != part.npos) {
      if (colon != address.npos) return false;
      unsigned octets = 0;
      auto tail = part;
      for (;;) {
        const auto dot = tail.find('.');
        const auto octet = tail.substr(0, dot);
        if (octet.empty() || octet.size() > 3 || (octet.size() > 1 && octet.front() == '0'))
          return false;
        unsigned number = 0;
        for (const char c : octet) {
          if (c < '0' || c > '9') return false;
          number = number * 10 + static_cast<unsigned>(c - '0');
        }
        if (number > 255 || ++octets > 4) return false;
        if (dot == tail.npos) break;
        tail.remove_prefix(dot + 1);
      }
      if (octets != 4) return false;
      groups += 2;
      break;
    }
    if (part.size() > 4 || part.find_first_not_of("0123456789abcdefABCDEF") != part.npos)
      return false;
    if (++groups > 8) return false;
    if (colon == address.npos) break;
    address.remove_prefix(colon + 1);
    if (address.empty()) return false;
    if (address.front() == ':') {
      if (compressed) return false;
      compressed = true;
      address.remove_prefix(1);
    }
  }
  return compressed ? groups < 8 : groups == 8;
}
}

// Views refer to the input URL. An origin omits user information; callers decide whether to accept it.
struct Url {
  std::string_view scheme;
  std::optional<std::string_view> userinfo;
  std::string_view host;
  std::string_view port;
  std::string_view target = "/";
  std::optional<std::string_view> fragment;
  std::uint16_t effectivePort = 80;
  bool hasPath = false;

  bool tls() const { return scheme == "https"; }
  bool originOnly() const { return !hasPath && target == "/" && !fragment; }

  std::string origin() const {
    std::string result(scheme);
    result += "://";
    result += host;
    if (!port.empty()) { result += ':'; result += port; }
    return result;
  }

  std::string requestTarget() const {
    std::string result;
    if (target.front() == '?') result = "/";
    result += target;
    return result;
  }
};

inline std::optional<Url> parseUrl(std::string_view text) {
  for (const unsigned char c : text)
    if (c <= 0x20 || c == 0x7f || c == '\\') return std::nullopt;
  const auto separator = text.find("://");
  if (separator != 4 && separator != 5) return std::nullopt;
  constexpr std::string_view https = "https";
  for (std::size_t i = 0; i < separator; ++i) {
    char c = text[i];
    if (c >= 'A' && c <= 'Z') c += 'a' - 'A';
    if (c != https[i]) return std::nullopt;
  }
  Url result;
  result.scheme = separator == 5 ? "https" : "http";
  result.effectivePort = result.tls() ? 443 : 80;
  const auto start = separator + 3;
  const auto end = text.find_first_of("/?#", start);
  auto authority = text.substr(start, end == text.npos ? text.npos : end - start);
  const auto at = authority.find('@');
  if (at != authority.npos) {
    result.userinfo = authority.substr(0, at);
    authority.remove_prefix(at + 1);
    if (authority.find('@') != authority.npos) return std::nullopt;
  }
  if (authority.empty()) return std::nullopt;
  std::size_t hostEnd;
  if (authority.front() == '[') {
    const auto close = authority.find(']');
    if (close == authority.npos) return std::nullopt;
    const auto address = authority.substr(1, close - 1);
    if (!url_detail::ipv6(address)) return std::nullopt;
    hostEnd = close + 1;
    if (hostEnd < authority.size() && authority[hostEnd] != ':') return std::nullopt;
  } else {
    hostEnd = authority.find(':');
    if (hostEnd == authority.npos) hostEnd = authority.size();
    const auto name = authority.substr(0, hostEnd);
    for (const unsigned char c : name)
      if (!((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
            (c >= '0' && c <= '9') || c == '.' || c == '-' || c == '_')) return std::nullopt;
  }
  result.host = authority.substr(0, hostEnd);
  if (result.host.empty() || result.host.size() > 253) return std::nullopt;
  if (hostEnd < authority.size()) {
    result.port = authority.substr(hostEnd + 1);
    if (result.port.empty() || result.port.size() > 5) return std::nullopt;
    unsigned number = 0;
    for (const char c : result.port) {
      if (c < '0' || c > '9') return std::nullopt;
      number = number * 10 + static_cast<unsigned>(c - '0');
    }
    if (!number || number > 65535) return std::nullopt;
    result.effectivePort = static_cast<std::uint16_t>(number);
  }
  if (end != text.npos) {
    const auto hash = text.find('#', end);
    if (hash != text.npos) result.fragment = text.substr(hash + 1);
    const auto target = text.substr(end, hash == text.npos ? text.npos : hash - end);
    result.hasPath = !target.empty() && target.front() == '/';
    if (!target.empty()) result.target = target;
  }
  return result;
}

}
