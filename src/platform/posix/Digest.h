#pragma once

#include <string>
#include <string_view>

namespace awtrix::posix {

std::string sha256Hex(std::string_view text);

}
