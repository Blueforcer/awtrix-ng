#pragma once

#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

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
    skipBlank<AttributeSyntax::ClosedQuotes>(line, i);
    if (i >= line.size()) break;
    HeaderAttribute attribute;
    if (!readAttr<AttributeSyntax::ClosedQuotes>(line, i, attribute.key, attribute.value, &attribute.hasValue))
      return false;
    out.push_back(std::move(attribute));
  }
  return true;
}

inline std::vector<std::string> headerLines(const std::string& source, std::string_view tag) {
  std::vector<std::string> lines;
  forEachHeaderTag(source, [&](const std::string& name, const std::string& value, int) {
    if (name == tag) lines.push_back(value);
  });
  return lines;
}

inline std::nullopt_t specFailure(std::string* error, const char* message) {
  if (error) *error = message;
  return std::nullopt;
}

}
