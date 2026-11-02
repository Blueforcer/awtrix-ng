#pragma once

#include <cstddef>
#include <string>
#include <string_view>

#include "core/StrCase.h"

namespace awtrix::net {

inline std::string mediaType(std::string_view contentType) {
  constexpr std::string_view kSpace = " \t";
  const std::string_view type = contentType.substr(0, contentType.find(';'));
  const std::size_t first = type.find_first_not_of(kSpace);
  if (first == std::string_view::npos) return {};
  return strcase::toLower(std::string(type.substr(first, type.find_last_not_of(kSpace) + 1 - first)));
}

}
