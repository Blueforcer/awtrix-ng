#pragma once

#include <string>
#include <vector>

namespace awtrix::hostapp {

struct AppPaths {
  std::string bundle;
  std::string host;
  std::string runner;
  std::string webui;
  std::string data;
  std::string log;
  std::string launchAgents;
};

AppPaths appPaths(const std::string& bundle, const std::string& home);
std::vector<std::string> hostArguments(const AppPaths& paths);

}
