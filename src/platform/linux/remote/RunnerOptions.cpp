#include "platform/linux/remote/RunnerOptions.h"

#include <cstdio>
#include <string>

#include "platform/posix/Text.h"

namespace awtrix::remote {
namespace {

int number(const char* value, int low, int high) {
  int out = -1;
  posix::parseInteger(value, low, high, out);
  return out;
}

void usage() {
  std::puts("awtrix-script-runner --data DIRECTORY --clock UID --key-file FILE --clock-url http://CLOCK\n"
            "  [--port 8301] [--width 32] [--height 8] [--webui FILE] [--ca-file FILE] [--idle SECONDS]\n"
            "Runs the scripts of one ESP32 clock for awtrix-host. Listens on every address; the key is the\n"
            "only login. SIGINT/SIGTERM save and stop. With --idle it saves and ends with status 3 once the\n"
            "clock was not heard for that long.");
}

bool setOnce(std::string& target, const char* value) {
  if (!value[0] || !target.empty()) return false;
  target = value;
  return true;
}

bool readValue(const std::string& arg, const char* value, RunnerOptions& o) {
  if (arg == "--data") return setOnce(o.data, value);
  if (arg == "--webui") { o.webui = value; return true; }
  if (arg == "--ca-file") return setOnce(o.caFile, value);
  if (arg == "--clock") return setOnce(o.clock, value);
  if (arg == "--key-file") return setOnce(o.keyFile, value);
  if (arg == "--clock-url") return setOnce(o.clockUrl, value);
  if (arg == "--port") o.port = number(value, 1024, 65535);
  else if (arg == "--width") o.width = number(value, 8, 128);
  else if (arg == "--height") o.height = number(value, 8, 32);
  else if (arg == "--idle") o.idleSeconds = number(value, 1, 86400);
  else return false;
  return true;
}

}

int parseRunnerOptions(int argc, char** argv, RunnerOptions& o) {
  for (int i = 1; i < argc; ++i) {
    const std::string arg = argv[i];
    if (arg == "--help") { usage(); return 0; }
    if (i + 1 == argc || !readValue(arg, argv[++i], o)) { usage(); return 2; }
  }
  if (o.data.empty() || o.clock.empty() || o.keyFile.empty() || o.clockUrl.empty() || o.port < 0 || o.width < 0 ||
      o.height < 0 || o.idleSeconds < 0) {
    usage();
    return 2;
  }
  return -1;
}

}
