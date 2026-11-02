#pragma once

#include <algorithm>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "core/StrCase.h"
#include "core/net/Url.h"

namespace awtrix::net {

inline std::vector<std::string> splitHosts(const std::string& list) {
  std::vector<std::string> hosts;
  std::size_t pos = 0;
  for (;;) {
    const std::size_t comma = list.find(',', pos);
    std::string host = strcase::toLower(list.substr(pos, comma == std::string::npos ? std::string::npos : comma - pos));
    if (!host.empty()) hosts.push_back(std::move(host));
    if (comma == std::string::npos) return hosts;
    pos = comma + 1;
  }
}

inline bool validHost(std::string_view host) {
  const std::string url = "https://" + std::string(host);
  const auto parsed = parseUrl(url);
  return parsed && parsed->host == host;
}

inline std::string hostOf(const std::string& url) {
  const auto parsed = parseUrl(url);
  return parsed && !parsed->userinfo ? strcase::toLower(std::string(parsed->host)) : std::string();
}

inline const char* urlRefusal(const std::string& url, const std::vector<std::string>& hosts,
                              bool (*plainAllowed)(std::string_view host) = nullptr) {
  const auto parsed = parseUrl(url);
  if (!parsed) return "http";
  const std::string host = strcase::toLower(std::string(parsed->host));
  if (!parsed->tls() && !(plainAllowed && plainAllowed(host))) return "http";
  if (parsed->userinfo) return "host";
  return std::find(hosts.begin(), hosts.end(), host) == hosts.end() ? "host" : nullptr;
}

}
