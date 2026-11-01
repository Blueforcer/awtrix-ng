#pragma once

#include <algorithm>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "core/StrCase.h"
#include "core/net/Url.h"
#include "core/script/ScriptMeta.h"

namespace awtrix::script {

struct HeaderAttribute {
  std::string key;
  std::string value;
  bool hasValue = false;
};

inline bool splitAttributes(const std::string& line, std::vector<HeaderAttribute>& out) {
  std::size_t i = 0;
  while (i < line.size()) {
    skipBlank<AttributeSyntax::OAuth>(line, i);
    if (i >= line.size()) break;
    HeaderAttribute attribute;
    if (!readAttr<AttributeSyntax::OAuth>(line, i, attribute.key, attribute.value, &attribute.hasValue))
      return false;
    out.push_back(std::move(attribute));
  }
  return true;
}

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
  return !host.empty() && host.size() <= 253 && std::all_of(host.begin(), host.end(), [](unsigned char c) {
    return c > ' ' && c < 127 && c != '/' && c != ':' && c != '@';
  });
}

inline std::string hostOf(const std::string& url) {
  const auto parsed = net::parseUrl(url);
  return parsed && !parsed->userinfo ? strcase::toLower(std::string(parsed->host)) : std::string();
}

inline const char* urlRefusal(const std::string& url, const std::vector<std::string>& hosts,
                              bool (*plainAllowed)(std::string_view host) = nullptr) {
  const auto parsed = net::parseUrl(url);
  if (!parsed) return "http";
  const std::string host = strcase::toLower(std::string(parsed->host));
  if (!parsed->tls() && !(plainAllowed && plainAllowed(host))) return "http";
  if (parsed->userinfo) return "host";
  if (hosts.empty()) return nullptr;
  return std::find(hosts.begin(), hosts.end(), host) == hosts.end() ? "host" : nullptr;
}

}
