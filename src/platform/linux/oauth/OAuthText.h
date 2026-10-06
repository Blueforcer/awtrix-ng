#pragma once

#include "core/StrCase.h"

#include <algorithm>
#include <cctype>
#include <string>
#include <string_view>

namespace awtrix::oauth {

inline std::string lowercase(const std::string& text) { return strcase::toLower(text); }

// Printable ASCII only: what URLs, client IDs, codes and header lines may hold.
inline bool printable(std::string_view text) {
  return std::all_of(text.begin(), text.end(), [](unsigned char c) { return c >= 32 && c < 127; });
}

inline bool equalsIgnoringCase(std::string_view a, std::string_view b) {
  return strcase::equalsIgnoreCase(a, b);
}

}
