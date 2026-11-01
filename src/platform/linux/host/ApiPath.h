#pragma once

#include <algorithm>
#include <string>
#include <string_view>

namespace awtrix::host {

inline bool splitApiPath(const std::string& path, std::string_view prefix, std::string& name, std::string& rest) {
  if (path.compare(0, prefix.size(), prefix) != 0 || (path.size() > prefix.size() && path[prefix.size()] != '/'))
    return false;
  const std::size_t start = std::min(path.size(), prefix.size() + 1);
  const std::size_t slash = path.find('/', start);
  name = path.substr(start, slash == std::string::npos ? std::string::npos : slash - start);
  rest = slash == std::string::npos ? std::string() : path.substr(slash + 1);
  return true;
}

}
