#pragma once

#include <string>

#include "platform/tc002/daemon/Process.h"

// Android property area inherited from init. ANDROID_PROPERTY_WORKSPACE="<fd>,<size>" names a
// read-only descriptor of the unlinked /dev/__properties__; /bin/setprop and /bin/getprop only
// work when a child receives both that variable and the open descriptor (otherwise they exit 0
// without doing anything). The daemon keeps the descriptor close-on-exec. setprop() builds
// the ProcessSpec that hands the descriptor and environment() to the child.
// awtrix-linux never receives it.
namespace awtrix {
namespace tc002d {

class PropertyWorkspace {
 public:
  // Validates the inherited descriptor (3..1023, regular file, O_RDONLY, at least <size> bytes)
  // and marks it close-on-exec. Call once at startup, before any descriptor sweep.
  static bool adopt();
  static bool available() { return fd() >= 0; }
  static int fd();
  // "ANDROID_PROPERTY_WORKSPACE=<fd>,<size>", empty when unavailable.
  static const std::string& environment();
  // program (setprop) with key and value, the workspace descriptor and variable, for spawnProcess();
  // false when there is no workspace, since setprop would then change nothing.
  static bool setprop(const std::string& program, const std::string& key, const std::string& value,
                      ProcessSpec& out);
};

}
}
