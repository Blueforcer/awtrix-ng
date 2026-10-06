#pragma once

#include <string>

#include "platform/tc002/daemon/Options.h"

// Why the first runtime of a daemon starts (RuntimeContract.h). Two markers carry it from one
// daemon to the next: <run-dir>/started, in RAM, which every daemon of a boot creates, and
// state/rebooted, which a daemon writes before it reboots the clock itself.
namespace awtrix {
namespace tc002d {

// software when a daemon ran before in this boot or the one before rebooted the clock, whose
// marker is used up then; else poweron. Creates the started marker.
std::string firstStartReason(const DaemonOptions& options);
// Records the reboot this daemon is about to do.
void markReboot(const DaemonOptions& options);

}
}
