#pragma once

#include <string>

namespace awtrix::hostapp {

bool fetchStatus(int port, std::string& body, int timeoutMs = 1000);

}
