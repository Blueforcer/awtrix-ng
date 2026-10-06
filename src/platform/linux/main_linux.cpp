#include <cstdio>
#include <exception>
#include <string_view>

#include "platform/linux/LinuxOptions.h"
#include "platform/linux/LinuxRuntime.h"
#ifdef AWTRIX_MCU_PREPARE
#include "platform/tc002/mcu/McuPrepare.h"
#endif

using namespace awtrix;

int main(int argc, char** argv) try {
#ifdef AWTRIX_MCU_PREPARE
  if (argc > 1 && std::string_view(argv[1]) == "--prepare-mcu") return tc002::mcu::prepare(argc - 1, argv + 1);
#endif
  LinuxOptions options;
  if (const int status = parseLinuxOptions(argc, argv, options); status >= 0) return status;
  LinuxRuntime runtime(std::move(options));
  if (const int status = runtime.start(); status >= 0) return status;
  runtime.run();
  return runtime.stop();
} catch (const std::exception& error) {
  std::fprintf(stderr, "Linux application failed: %s\n", error.what());
  return 1;
}
