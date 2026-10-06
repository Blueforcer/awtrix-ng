#include "platform/tc002/daemon/StartReason.h"

#include <unistd.h>

#include "platform/posix/Files.h"
#include "support.h"

using namespace awtrix::tc002d;
using tc002d_test::check;
using tc002d_test::exists;
using tc002d_test::TempDir;

namespace {

void startReasons() {
  TempDir dir;
  DaemonOptions options;
  options.runDir = dir / "run";
  options.data = dir / "data";
  check(awtrix::posix::makeDirectories(options.runDir, 0700) &&
            awtrix::posix::makeDirectories(options.data + "/state", 0700),
        "directories");
  check(firstStartReason(options) == "poweron", "the first daemon after power-on");
  check(exists(options.startedPath()), "which leaves its marker in the run directory");
  check(firstStartReason(options) == "software", "a later daemon of the same boot");

  ::unlink(options.startedPath().c_str());
  markReboot(options);
  check(exists(options.rebootMarkerPath()), "a daemon records the reboot it does");
  check(firstStartReason(options) == "software", "the boot after that reboot");
  check(!exists(options.rebootMarkerPath()), "uses the record up");

  ::unlink(options.startedPath().c_str());
  check(firstStartReason(options) == "poweron", "the next boot without a record is a power-on");
}

}

int main() {
  startReasons();
  return tc002d_test::finish("tc002d start");
}
