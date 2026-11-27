#pragma once

#include <cstddef>
#include <string>
#include <string_view>

namespace awtrix::hostapp::win32 {

inline constexpr std::size_t kTipChars = 127;

std::wstring menuLabel(std::string_view text);
std::wstring tipText(std::string_view text);

}
