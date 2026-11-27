#include "platform/host/Service.h"

#include <sys/stat.h>

#include <cstdio>

namespace awtrix::os {

int runService(const std::string&, std::atomic<bool>&, const ServiceBody& body) { return body(false); }

bool logToFile(const std::string& path, unsigned long keepBytes) {
  struct stat info{};
  if (::stat(path.c_str(), &info) == 0 && static_cast<unsigned long>(info.st_size) > keepBytes)
    std::rename(path.c_str(), (path + ".1").c_str());
  return std::freopen(path.c_str(), "a", stdout) && std::freopen(path.c_str(), "a", stderr);
}

}
