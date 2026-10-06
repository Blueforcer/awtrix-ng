#include "platform/tc002/daemon/SpeakerBackend.h"

#include <fcntl.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <unistd.h>

#include <cerrno>
#include <csignal>
#include <cstdio>
#include <cstring>

#include "platform/posix/Files.h"
#include "platform/tc002/daemon/Kernel.h"
#include "platform/tc002/daemon/Log.h"
#include "platform/tc002/daemon/Process.h"

namespace awtrix {
namespace tc002d {
namespace {

const std::string kNoHelper;

}

SpeakerBackend::SpeakerBackend(SpeakerOptions options) : options_(std::move(options)) {}

SpeakerBackend::~SpeakerBackend() {
  if (loader_ <= 0) return;
  ::kill(loader_, SIGKILL);
  reapUntil(loader_, posix::monotonicMs() + kReapGraceMs);
}

const char* SpeakerBackend::kindName() const {
  switch (kind_) {
    case Kind::None: return "none";
    case Kind::Loading: return "loading";
    case Kind::Pcm: return "pcm";
    case Kind::Off: return "off";
  }
  return "none";
}

const std::string& SpeakerBackend::helper() const {
  return kind_ == Kind::Pcm ? options_.pcm.helper : kNoHelper;
}

int SpeakerBackend::moduleListed() const {
  std::string modules;
  if (!posix::readText(options_.pcm.modules, modules, 256 * 1024)) return -1;
  return modules.rfind("awtrix_pcm ", 0) == 0 || modules.find("\nawtrix_pcm ") != std::string::npos ? 1 : 0;
}

void SpeakerBackend::choose(Kind kind, const std::string& note) {
  kind_ = kind;
  note_ = note;
  switch (kind) {
    case Kind::Pcm:
      Log::line("audio", "speaker backend: awtrix_pcm helper %s - %s", options_.pcm.helper.c_str(), note.c_str());
      break;
    case Kind::Off: Log::line("audio", "speaker backend: off until reboot - %s", note.c_str()); break;
    case Kind::None: Log::line("audio", "speaker backend: none - %s", note.c_str()); break;
    case Kind::Loading: Log::line("audio", "speaker backend: %s", note.c_str()); break;
  }
}

void SpeakerBackend::begin(int64_t nowMs) {
  kind_ = Kind::None;
  note_.clear();
  if (options_.pcm.helper.empty()) return;
  if (::access(options_.pcm.helper.c_str(), X_OK) < 0) {
    choose(Kind::Off, "awtrix_pcm helper " + options_.pcm.helper + " unavailable: " + std::strerror(errno));
    return;
  }
  const int listed = moduleListed();
  if (listed > 0) {
    choose(Kind::Pcm, "awtrix_pcm was already loaded");
    return;
  }
  if (listed < 0) {
    choose(Kind::Off, "cannot read " + options_.pcm.modules + ", so the module state is unknown");
    return;
  }
  struct stat info{};
  if (::lstat(options_.pcm.module.c_str(), &info) < 0 || !trustedModuleFile(info, options_.owner)) {
    choose(Kind::Off, "module " + options_.pcm.module + " is missing or not a regular file only its owner may write");
    return;
  }
  startLoader(nowMs);
}

void SpeakerBackend::startLoader(int64_t nowMs) {
  const std::string& module = options_.pcm.module;
  const uid_t owner = options_.owner;
  const std::function<int(int)>& load = options_.loadModule;
  const pid_t pid = forkTask(
      [&]() {
        ModuleLoad hook;
        if (load) hook = [&](int fd, const std::string&) { return load(fd); };
        return loadKernelModule(module, "", owner, hook);
      },
      SIGKILL);
  if (pid < 0) {
    choose(Kind::Off, std::string("cannot fork the module loader: ") + std::strerror(errno));
    return;
  }
  loader_ = pid;
  loadStartedAt_ = nowMs;
  loadDeadline_ = nowMs + options_.loadTimeoutMs;
  choose(Kind::Loading, "loading " + options_.pcm.module + " (loader pid " + std::to_string(pid) + ")");
}

int64_t SpeakerBackend::nextDeadlineMs() const { return kind_ == Kind::Loading ? loadDeadline_ : -1; }

void SpeakerBackend::onTime(int64_t nowMs) {
  if (kind_ == Kind::Loading && loadDeadline_ >= 0 && nowMs >= loadDeadline_) {
    ::kill(loader_, SIGKILL);
    loadDeadline_ = -1;
    choose(Kind::Off, "loading awtrix_pcm did not finish within " + std::to_string(options_.loadTimeoutMs) +
                          " ms, so its state is unknown");
  }
}

bool SpeakerBackend::onChildExit(pid_t pid, int status, int64_t nowMs) {
  if (loader_ <= 0 || pid != loader_) return false;
  loader_ = -1;
  if (kind_ != Kind::Loading) {
    Log::line("audio", "module loader pid %d ended late (%s)", static_cast<int>(pid), describeWait(status).c_str());
    return true;
  }
  loadDeadline_ = -1;
  const int listed = moduleListed();
  const int error = WIFEXITED(status) ? WEXITSTATUS(status) : -1;
  if (error == 0 && listed > 0) {
    choose(Kind::Pcm, "awtrix_pcm loaded in " + std::to_string(nowMs - loadStartedAt_) + " ms");
    return true;
  }
  std::string why;
  if (error == 0) why = "the loader succeeded but awtrix_pcm is not listed in " + options_.pcm.modules;
  else if (error > 0) why = "loading " + options_.pcm.module + " failed: " + std::strerror(error);
  else why = "the module loader ended with " + describeWait(status);
  if (listed > 0) why += "; awtrix_pcm is listed";
  else if (listed < 0) why += "; " + options_.pcm.modules + " is unreadable";
  choose(Kind::Off, why);
  return true;
}

void SpeakerBackend::helperEnded(int status) {
  if (kind_ != Kind::Pcm || (WIFEXITED(status) && WEXITSTATUS(status) == 0)) return;
  choose(Kind::Off, "the awtrix_pcm helper failed (" + describeWait(status) + ")");
}

}
}
