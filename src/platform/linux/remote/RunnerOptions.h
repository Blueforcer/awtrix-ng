#pragma once

#include <string>

namespace awtrix::remote {

inline constexpr int kClockSilentExit = 3;

struct RunnerOptions {
  std::string data, webui = "webui/index.html", caFile;
  std::string clock, keyFile, clockUrl;
  int port = 8301, width = 32, height = 8, idleSeconds = 0;
};

int parseRunnerOptions(int argc, char** argv, RunnerOptions& options);

}
