#pragma once

#include <sys/types.h>

#include <string>
#include <vector>

namespace awtrix {
namespace tc002d {
namespace ip {

struct SpawnRequest {
  std::string path;
  std::vector<std::string> argv;
  std::vector<std::string> environment;
  int passFd = -1;    // duplicated to passAs in the child; every other descriptor above 2 closes
  int passAs = -1;
  bool discardStdout = false;
  int stderrFd = -1;  // the child's stderr; -1 keeps the daemon's
};

// Starts path through spawnProcess() in its own process group; the child is killed if the daemon
// dies. Returns -1 on failure.
pid_t spawnChild(const SpawnRequest& request, std::string& error);

}
}
}
