#pragma once

#include <string>

namespace awtrix::os {

bool logToFile(const std::string& path, unsigned long keepBytes);

}
