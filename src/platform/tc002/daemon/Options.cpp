#include "platform/tc002/daemon/Options.h"
#include "platform/posix/Text.h"

#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>

#include <cerrno>
#include <cstdlib>
#include <cstring>

namespace awtrix {
namespace tc002d {
namespace {

bool absolute(const std::string& path) { return !path.empty() && path[0] == '/'; }

void trimSlashes(std::string& path) {
  while (path.size() > 1 && path.back() == '/') path.pop_back();
}

}

const char* bootIntroName(BootIntro intro) {
  switch (intro) {
    case BootIntro::First: return "first";
    case BootIntro::Always: return "always";
    case BootIntro::Never: return "never";
  }
  return "first";
}

IntroStarts DaemonOptions::introStarts() const {
  switch (bootIntro) {
    case BootIntro::First: return IntroStarts::First;
    case BootIntro::Always: return IntroStarts::Every;
    case BootIntro::Never: break;
  }
  return IntroStarts::None;
}

std::vector<std::string> DaemonOptions::bootAttemptsPaths() const {
  return {data + TC002_BOOT_ATTEMPTS, sysRoot + TC002_VOLATILE_DIR TC002_VOLATILE_BOOT_ATTEMPTS};
}

std::string DaemonOptions::webIndexPath() const {
  const std::string compressed = root + "/share/index.html.gz";
  return regularFile(compressed) ? compressed : root + "/share/index.html";
}

PcmBackendPaths DaemonOptions::pcmBackend() const {
  PcmBackendPaths paths;
  paths.helper = root + "/bin/awtrix-tc002-audio-pcm";
  paths.module = moduleDirectory() + "/awtrix_pcm.ko";
  paths.modules = sysRoot + "/proc/modules";
  return paths;
}

std::vector<std::string> DaemonOptions::missingComponents() const {
  const PcmBackendPaths pcm = pcmBackend();
  std::vector<std::string> missing;
  for (const std::string& program : {runtimePath(), supplicantPath(), udhcpcPath(), dhcpCallbackPath(), pcm.helper})
    if (!regularFile(program) || ::access(program.c_str(), X_OK) != 0) missing.push_back(program);
  for (const std::string& module :
       {moduleDirectory() + "/aic8800_bsp.ko", moduleDirectory() + "/aic8800_fdrv.ko", pcm.module})
    if (!regularFile(module)) missing.push_back(module);
  return missing;
}

IntroStarts claimPowerOnIntro(IntroStarts starts, const std::string& marker) {
  if (starts != IntroStarts::First) return starts;
  const int fd = ::open(marker.c_str(), O_WRONLY | O_CREAT | O_EXCL | O_NOFOLLOW | O_CLOEXEC, 0600);
  if (fd < 0) return IntroStarts::None;
  ::close(fd);
  return IntroStarts::First;
}

const char* usageText() {
  return "usage: awtrix-tc002d --root DIR --data DIR --boot [--http-port 80]\n"
         "                     [--run-dir " TC002_VOLATILE_DIR TC002_DAEMON_RUN "] [--uart " TC002_MCU_UART
         "] [--sys-root PREFIX]\n"
         "                     [--boot-intro first|always|never]\n"
         "                     [--update-dir " TC002_VOLATILE_DIR TC002_UPDATE_WORK "]\n"
         "       awtrix-tc002d ctl [--run-dir DIR] [--timeout MS] COMMAND [PAYLOAD | -]\n"
         "       awtrix-tc002d --version\n";
}

bool parseDaemonOptions(int argc, const char* const* argv, DaemonOptions& out, std::string& error) {
  out = DaemonOptions{};
  error.clear();
  bool boot = false;
  for (int i = 1; i < argc; ++i) {
    const std::string arg = argv[i];
    const auto value = [&](std::string& target) {
      if (i + 1 >= argc) {
        error = arg + " needs a value";
        return false;
      }
      target = argv[++i];
      return true;
    };
    std::string text;
    if (arg == "--boot") boot = true;
    else if (arg == "--root") { if (!value(out.root)) return false; }
    else if (arg == "--data") { if (!value(out.data)) return false; }
    else if (arg == "--run-dir") { if (!value(out.runDir)) return false; }
    else if (arg == "--uart") { if (!value(out.uart)) return false; }
    else if (arg == "--sys-root") { if (!value(out.sysRoot)) return false; }
    else if (arg == "--update-dir") { if (!value(out.updateDir)) return false; }
    else if (arg == "--boot-intro") {
      if (!value(text)) return false;
      if (text == "first") out.bootIntro = BootIntro::First;
      else if (text == "always") out.bootIntro = BootIntro::Always;
      else if (text == "never") out.bootIntro = BootIntro::Never;
      else {
        error = "--boot-intro must be first, always or never";
        return false;
      }
    } else if (arg == "--http-port") {
      if (!value(text)) return false;
      if (!posix::parseInteger(text.c_str(), 1, 65535, out.httpPort)) {
        error = "--http-port must be 1..65535";
        return false;
      }
    } else {
      error = "unknown option " + arg;
      return false;
    }
  }
  if (!absolute(out.root)) error = "--root must be an absolute directory";
  else if (!absolute(out.data)) error = "--data must be an absolute directory";
  else if (!absolute(out.runDir)) error = "--run-dir must be absolute";
  else if (!out.sysRoot.empty() && !absolute(out.sysRoot)) error = "--sys-root must be absolute";
  else if (!absolute(out.updateDir)) error = "--update-dir must be absolute";
  else if (!boot)
    error = "--boot is required: the daemon runs only as the service the loader starts";
  if (!error.empty()) return false;
  trimSlashes(out.root);
  trimSlashes(out.data);
  trimSlashes(out.runDir);
  trimSlashes(out.updateDir);
  return true;
}

bool regularFile(const std::string& path) {
  struct stat info{};
  return ::stat(path.c_str(), &info) == 0 && S_ISREG(info.st_mode);
}

bool developerFlagPresent(const std::string& path, uid_t owner) {
  struct stat info{};
  return ::lstat(path.c_str(), &info) == 0 && S_ISREG(info.st_mode) && info.st_uid == owner;
}

bool parseCtlOptions(int argc, const char* const* argv, CtlOptions& out, std::string& error) {
  out = CtlOptions{};
  int i = 1;
  for (; i < argc; ++i) {
    const std::string arg = argv[i];
    if (arg == "--run-dir" && i + 1 < argc) out.runDir = argv[++i];
    else if (arg == "--timeout" && i + 1 < argc) {
      if (!posix::parseInteger(argv[++i], 100, 600000, out.timeoutMs)) {
        error = "--timeout must be 100..600000 ms";
        return false;
      }
    } else if (arg.size() > 1 && arg[0] == '-' && arg[1] == '-') {
      error = "unknown ctl option " + arg;
      return false;
    } else {
      break;
    }
  }
  if (i >= argc) {
    error = "ctl needs a command";
    return false;
  }
  out.command = argv[i++];
  if (i < argc) {
    if (!std::strcmp(argv[i], "-")) out.payloadFromStdin = true;
    else out.payload = argv[i];
    ++i;
  }
  if (i < argc) {
    error = "ctl takes one command and at most one payload";
    return false;
  }
  if (out.command.empty() || out.command.find('\n') != std::string::npos) {
    error = "invalid command";
    return false;
  }
  if (!absolute(out.runDir)) {
    error = "--run-dir must be absolute";
    return false;
  }
  return true;
}

}
}
