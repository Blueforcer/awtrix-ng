#include "platform/host/app/macos/AppPaths.h"

namespace awtrix::hostapp {

AppPaths appPaths(const std::string& bundle, const std::string& home) {
  AppPaths paths;
  paths.bundle = bundle;
  paths.host = bundle + "/Contents/MacOS/awtrix-host";
  paths.runner = bundle + "/Contents/MacOS/awtrix-script-runner";
  paths.webui = bundle + "/Contents/Resources/index.html";
  paths.data = home + "/Library/Application Support/AWTRIX Script Host";
  paths.log = home + "/Library/Logs/AWTRIX Script Host/awtrix-host.log";
  paths.launchAgents = home + "/Library/LaunchAgents";
  return paths;
}

std::vector<std::string> hostArguments(const AppPaths& paths) {
  return {paths.host,   "--state", paths.data,   "--log", paths.log, "--exit-with-stdin", "--binary",
          paths.runner, "--webui", paths.webui};
}

}
