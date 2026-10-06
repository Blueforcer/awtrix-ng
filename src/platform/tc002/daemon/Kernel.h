#pragma once

#include <sys/stat.h>
#include <sys/types.h>

#include <functional>
#include <string>

namespace awtrix {
namespace tc002d {

// Replaces finit_module in tests: gets the checked module descriptor and the parameters.
using ModuleLoad = std::function<int(int fd, const std::string& parameters)>;

// A regular file of owner that nobody else may write.
bool trustedModuleFile(const struct stat& info, uid_t owner);

// Loads a kernel module the one way the daemon does: path is opened without following a symlink
// and must be a trustedModuleFile(). Returns 0, also when the
// module is already loaded, or an errno value. Blocks while the module initialises, so it runs
// in a forkTask().
int loadKernelModule(const std::string& path, const std::string& parameters, uid_t owner,
                     const ModuleLoad& load = nullptr);

// Sets kernel.panic to the given seconds and kernel.panic_on_oops to 1 below sysRoot, so an oops
// reboots the device instead of leaving it hung. Returns an empty string, or what failed.
std::string armPanicReboot(const std::string& sysRoot, int seconds = 5);

}
}
