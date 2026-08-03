#pragma once

#include <string>
#include <utility>
#include <vector>

namespace awtrix::script {

using HttpHeaders = std::vector<std::pair<std::string, std::string>>;

// Headers cross the script boundary as "Name: value" entries joined by this. A control
// character on purpose: a value holding one is rejected instead of splitting into two headers.
constexpr char kHeaderSeparator = '\x1f';

bool normalizeMethod(const std::string& in, std::string& out);

bool headerAllowed(const std::string& name);

bool parseHeaderBlock(const std::string& block, HttpHeaders& out);

}
