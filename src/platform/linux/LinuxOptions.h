#pragma once

#include <string>

#include "platform/linux/LinuxAdminSecurity.h"
#include "platform/tc002/runtime/Tc002Update.h"

namespace awtrix {

// The command line of awtrix-linux.
struct LinuxOptions {
  std::string data, webui = "webui/index.html", boardType = "headless", performancePath;
  LinuxAdminConfig administration;
  int port = 8080, width = 52, height = 16;
  bool physicalInput = false;
  bool supervised = false;
  bool speakerRequested = false;
  bool bluetooth = false;
  bool bootIntro = false;
  std::string bootSound;
  std::string speechVoice;
  std::string caFile;
  std::string startReason;
  std::string uid;
  Tc002UpdateOptions updateOptions;

  bool webUpdate() const { return !updateOptions.statePath.empty(); }
};

// Reads and checks the command line: -1 to start, otherwise the exit code.
int parseLinuxOptions(int argc, char** argv, LinuxOptions& options);

}
