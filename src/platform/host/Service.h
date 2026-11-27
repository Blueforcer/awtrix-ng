#pragma once

#include <atomic>
#include <functional>
#include <string>

namespace awtrix::os {

using ServiceBody = std::function<int(bool asService)>;

int runService(const std::string& name, std::atomic<bool>& stop, const ServiceBody& body);
bool logToFile(const std::string& path, unsigned long keepBytes);

}
