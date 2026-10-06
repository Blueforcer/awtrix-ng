#include "platform/tc002/daemon/ip/ChildProcess.h"

#include <unistd.h>

#include <csignal>

#include "platform/tc002/daemon/Process.h"

namespace awtrix {
namespace tc002d {
namespace ip {

pid_t spawnChild(const SpawnRequest& request, std::string& error) {
  if (request.argv.empty() || (request.passFd >= 0 && request.passAs <= STDERR_FILENO)) {
    error = "invalid spawn request";
    return -1;
  }
  ProcessSpec spec;
  spec.path = request.path;
  spec.argv = request.argv;
  spec.environment = request.environment;
  if (!request.discardStdout) spec.descriptors.push_back({STDOUT_FILENO, STDOUT_FILENO});
  spec.descriptors.push_back({request.stderrFd >= 0 ? request.stderrFd : STDERR_FILENO, STDERR_FILENO});
  if (request.passFd >= 0) spec.descriptors.push_back({request.passFd, request.passAs});
  spec.group = ProcessGroup::Own;
  spec.parentDeathSignal = SIGKILL;
  return spawnProcess(spec, error);
}

}
}
}
