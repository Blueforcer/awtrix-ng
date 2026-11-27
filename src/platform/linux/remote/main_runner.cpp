#include <cstdio>
#include <exception>

#include "platform/linux/remote/RunnerOptions.h"
#include "platform/linux/remote/ScriptRunner.h"

using namespace awtrix::remote;

int main(int argc, char** argv) try {
  RunnerOptions options;
  if (const int status = parseRunnerOptions(argc, argv, options); status >= 0) return status;
  ScriptRunner runner(std::move(options));
  if (const int status = runner.start(); status >= 0) return status;
  runner.run();
  const int status = runner.stop();
  return status == 0 && runner.silent() ? kClockSilentExit : status;
} catch (const std::exception& error) {
  std::fprintf(stderr, "Script runner failed: %s\n", error.what());
  return 1;
}
