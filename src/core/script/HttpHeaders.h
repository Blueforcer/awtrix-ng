#pragma once

#include <algorithm>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "core/StrCase.h"

namespace awtrix::script {

using HttpHeaders = std::vector<std::pair<std::string, std::string>>;

// Headers cross the script boundary as "Name: value" entries joined by this. A control
// character on purpose: a value holding one is rejected instead of splitting into two headers.
constexpr char kHeaderSeparator = '\x1f';

constexpr bool headerValueChar(char c) {
  const auto u = static_cast<unsigned char>(c);
  return u >= 0x20 && u != 0x7f;
}

bool normalizeMethod(const std::string& in, std::string& out);

bool headerAllowed(const std::string& name);

bool parseHeaderBlock(const std::string& block, HttpHeaders& out);

inline bool hasHeader(const HttpHeaders& headers, std::string_view name) {
  return std::any_of(headers.begin(), headers.end(),
                     [name](const auto& header) { return strcase::equalsIgnoreCase(header.first, name); });
}

}
